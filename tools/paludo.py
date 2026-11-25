#!/usr/bin/env python3
"""Adaptador do contrato real Paludo 0.8.0-rc.2; nenhum offset ou IP fixo."""
import argparse
import hashlib
import http.cookiejar
import io
import json
import os
import re
import time
import urllib.parse
import urllib.request
import urllib.error
from pathlib import Path
from package import check_image


class Console:
    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=.25, write_timeout=5)
        self.serial.rts = self.serial.dtr = True
        self.serial.port = port
        # A reset briefly removes/recreates the macOS device node. Wait only
        # for open(), before any command is sent; never replay mutations.
        deadline=time.monotonic()+10
        while True:
            try:
                self.serial.open()
                break
            except (serial.SerialException,OSError) as error:
                if getattr(error,'errno',None) not in (2,6) or time.monotonic()>=deadline:
                    raise
                time.sleep(.1)
        self.serial.rts = self.serial.dtr = False
        self.serial.write(b"\r\n")
        time.sleep(.3)
        self.serial.reset_input_buffer()

    def close(self):
        self.serial.close()

    def command(self, *args):
        values = []
        for arg in args:
            value = str(arg)
            if any(ord(c) < 32 for c in value):
                raise ValueError("caractere de controle")
            values.append('"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"')
        line_command = ("tab5 " + " ".join(values) + "\r\n").encode()
        if len(line_command) >= 768:
            raise ValueError("comando longo demais")
        self.serial.write(line_command)
        # Some USB Serial/JTAG reconnects reset the chip. A status sent during
        # boot is lost before the console task starts. Retry only this read;
        # never resend boot/execute or any other mutating command.
        deadline = time.monotonic() + (40 if args == ("status",) else 10)
        next_status = time.monotonic() + 1
        while time.monotonic() < deadline:
            line = self.serial.readline().decode(errors="replace")
            start = line.find("TAB5 {")
            if start >= 0:
                try:
                    reply = json.loads(line[start + 5:])
                except json.JSONDecodeError:
                    continue
                if reply.get("error"):
                    raise RuntimeError(reply)
                # Startup status retries can leave an extra read reply in
                # the UART queue. Do not mistake it for web-start or boot.
                if args and args[0] == "status" and "version" not in reply:
                    continue
                if args and args[0] == "web-start" and not {"ip","code"} <= reply.keys():
                    continue
                if args and args[0] in ("web-stop","boot") and ("version" in reply or "code" in reply):
                    continue
                return reply
            if args == ("status",) and time.monotonic() >= next_status:
                self.serial.write(line_command)
                next_status = time.monotonic() + 1
        raise TimeoutError("console sem resposta")


class Launcher:
    @staticmethod
    def start_web_when_ready(console):
        # The console is ready before the C6/Wi-Fi startup completes.
        # web-start is idempotent; retry only its explicit NOT_READY state.
        deadline=time.monotonic()+45
        reconnected=False
        while True:
            try:return console.command('web-start')
            except RuntimeError as error:
                reply=error.args[0] if error.args else None
                if not isinstance(reply,dict) or reply.get('error')!=259:
                    raise
                if time.monotonic()>=deadline:
                    if reconnected:
                        raise RuntimeError('Launcher sem Wi-Fi; conecte pela tela de redes. Nenhuma gravação iniciada.') from error
                    # The real 0.8.0-rc.2 console supports reconnecting the
                    # saved network. One attempt only, before upload/flash.
                    console.command('wifi-retry')
                    reconnected=True
                    deadline=time.monotonic()+45
                time.sleep(1)

    def __init__(self, port=None, url=None, token=None):
        if port:
            console = Console(port)
            try:
                status = console.command("status")
                if status.get("version") != "0.8.0-rc.2":
                    raise ValueError("contrato de outra versão precisa de auditoria")
                web = self.start_web_when_ready(console)
                url, token = "http://" + web["ip"], web["code"]
            finally:
                console.close()
        if not url or not token:
            raise ValueError("informe --port ou PALUDO_URL e PALUDO_TOKEN")
        self.url, self.token = url.rstrip("/"), token
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        try:
            self.api("/api/session", {})
        except urllib.error.HTTPError as error:
            if error.code != 401 or not port:
                raise
            # Paludo 0.8.0-rc.2 web-start returns the existing code even after
            # its 15-minute pairing lifetime. Renew only the Web service via
            # the real console contract, before any upload/flash operation.
            console = Console(port)
            try:
                console.command("web-stop")
                web = self.start_web_when_ready(console)
            finally:
                console.close()
            self.url, self.token = "http://" + web["ip"], web["code"]
            self.api("/api/session", {})

    def api(self, path, data=None, raw=False):
        headers = {"X-Launcher-Token": self.token, "X-Launcher-Client": "web"}
        if isinstance(data, dict):
            headers["Content-Type"] = "application/json"
            data = json.dumps(data).encode()
        elif data is not None:
            headers["Content-Type"] = "application/octet-stream"
        stream = None
        if isinstance(data, bytes) and len(data) > 4096:
            # Keep Content-Length (the Launcher does not accept chunked bodies),
            # but let http.client send bounded blocks. A single sendall(4 MiB)
            # otherwise has a 90-second total timeout even while progressing.
            headers["Content-Length"] = str(len(data))
            stream = io.BytesIO(data)
            data = stream
        try:
            with self.opener.open(urllib.request.Request(self.url + path, data=data, headers=headers), timeout=90) as r:
                reply = r.read()
        finally:
            if stream is not None:
                stream.close()
        return reply if raw else json.loads(reply)

    def inspect(self):
        return {"apps": self.api("/api/apps"), "layout": self.api("/api/flash-layout")}

    def upload_image(self, image):
        data = check_image(image)
        digest = hashlib.sha256(data).hexdigest()
        name = "tab5-emu-" + digest[:16] + ".bin"
        remote = "/sdcard/APP/" + name
        uploaded = self.api("/api/upload?" + urllib.parse.urlencode({"directory": "/sdcard/APP", "relative": name}), data)
        if not uploaded.get("verified"):
            raise RuntimeError("launcher não confirmou upload")
        readback = self.api("/api/file?" + urllib.parse.urlencode({"path": remote}), raw=True)
        if hashlib.sha256(readback).hexdigest() != digest:
            raise RuntimeError("hash recebido difere")
        return {"remote":remote,"size":len(data),"sha256":digest,"uploaded":True,
                "received_hash_verified":True,"installed":False}

    def stage(self, image):
        uploaded = self.upload_image(image)
        plan = self.api("/api/operation", {"action": "install", "path": uploaded['remote']})
        if not plan.get("ok") or not plan.get("ticket"):
            raise RuntimeError(plan)
        return dict(uploaded,plan=plan)

    def execute_plan(self, plan):
        result=self.api('/api/operation',{'action':'execute','ticket':plan['ticket']})
        if not result.get('ok'):raise RuntimeError(result)
        deadline=time.monotonic()+240
        previous_progress=None
        while time.monotonic()<deadline:
            time.sleep(1)
            try:
                progress=self.api('/api/operation')
            except (ConnectionError, TimeoutError, urllib.error.URLError) as error:
                if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 502, 503, 504):
                    raise
                # Only repeat the read of the same accepted operation. Never
                # resend execute: the device may already have changed flash.
                print('Launcher: conexão de progresso interrompida; consultando a mesma operação.',flush=True)
                continue
            current=(progress.get('percent'),progress.get('stage') or progress.get('message'))
            if current != previous_progress:
                print('Launcher: '+str(current[0])+'%'+(' '+str(current[1]) if current[1] else ''),flush=True)
                previous_progress=current
            if progress.get('running'):continue
            if progress.get('error') or progress.get('percent')!=100:
                raise RuntimeError('operação não confirmada; inspecione antes de repetir: '+str(progress))
            return progress
        raise TimeoutError('operação ambígua; inspecione antes de repetir')

    def update(self,image):
        # This launcher's update flow is backup + erase the owned slot + install
        # into the same reservation. There is no direct overwrite API.
        data=check_image(image)
        before=self.inspect()
        own=[a for a in before['apps'] if a.get('descriptorValid') and a.get('name')=='tab5-emu']
        if len(own)!=1:raise ValueError('atualização exige exatamente um app tab5-emu instalado')
        slot=own[0]['slot']
        if len(data)>before['layout']['sizes'][slot]:raise ValueError('novo app excede o slot; nenhuma remoção realizada')
        # Paludo's allocation_model() rounds every requested image to 64 KiB,
        # including smaller updates: a shrink also requests a new table.
        rounded=(len(data)+65535)&~65535
        if rounded!=before['layout']['sizes'][slot]:
            raise ValueError('tamanho arredondado do novo app mudaria a reserva; nenhuma remoção realizada')
        minimum=before['layout'].get('minimum')
        if minimum and any(a.get('descriptorValid') and a['slot']!=slot and
                           minimum[a['slot']]!=before['layout']['sizes'][a['slot']]
                           for a in before['apps']):
            raise ValueError('o alocador compactaria outra reserva; nenhuma remoção realizada')
        if any(not a.get('descriptorValid') and a['slot']<slot for a in before['apps']):
            raise ValueError('slot vazio anterior mudaria a escolha do alocador; nenhuma remoção realizada')
        staged=self.upload_image(image)
        plan=self.api('/api/operation',{'action':'uninstall','slot':slot,'backupBinary':True,'backupData':False})
        if not plan.get('ok') or not plan.get('ticket') or not plan.get('message','').startswith(f'Remover app {slot} com backup do binario.'):
            raise RuntimeError('contrato de backup/remoção inesperado: '+str(plan))
        # User-authorized update: only this project's app is removed. Launcher
        # refuses erasure when its verified backup cannot be completed.
        print('Atualizar somente tab5-emu: '+plan['message'],flush=True)
        removed=self.execute_plan(plan)
        blank=self.inspect()
        if not any(a['slot']==slot and not a.get('descriptorValid') for a in blank['apps']):
            raise RuntimeError('slot vazio não confirmado; backup preservado')
        plan=self.api('/api/operation',{'action':'install','path':staged['remote']})
        if not plan.get('ok') or not plan.get('ticket') or f'slot {slot} (' not in plan.get('message','') or 'Espaco disponivel; apps existentes preservados.' not in plan.get('message',''):
            raise RuntimeError('novo plano não usa a reserva original; backup preservado: '+str(plan))
        installed=self.execute_plan(plan)
        after=self.inspect()
        for app in before['apps']:
            if app['slot']!=slot and app not in after['apps']:raise RuntimeError('outro app mudou; inspecionar')
        if before['layout']['sizes']!=after['layout']['sizes'] or before['layout']['launcher']!=after['layout']['launcher']:
            raise RuntimeError('reservas de flash mudaram; inspecionar')
        if not any(a['slot']==slot and a.get('name')=='tab5-emu' and a.get('descriptorValid') for a in after['apps']):
            raise RuntimeError('atualização não confirmada')
        return dict(staged,installed=True,booted=False,before=before,after=after,
                    backup_and_remove=removed,result=installed,slot=slot)

    def install(self, image):
        before = self.inspect()
        staged = self.stage(image)
        plan = staged["plan"]
        message = plan.get("message", "")
        # 0.8.0-rc.2 exposes needs_layout through this exact message, not a
        # structured flag. Fail closed on any message/contract change.
        match = re.search(r"slot (\d+) \(", message)
        if not match or "Espaco disponivel; apps existentes preservados." not in message:
            raise RuntimeError("plano exige reorganização ou contrato desconhecido: " + message)
        slot = int(match[1])
        target = next((a for a in before["apps"] if a["slot"] == slot), None)
        if target and target.get("descriptorValid") and target.get("name") != "tab5-emu":
            raise RuntimeError("destino contém outro aplicativo: " + target["name"])
        print("Destino: " + message, flush=True)
        result = self.api("/api/operation", {"action": "execute", "ticket": plan["ticket"]})
        if not result.get("ok"):
            raise RuntimeError(result)
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            time.sleep(1)
            progress = self.api("/api/operation")
            if progress.get("running"):
                continue
            if progress.get("error") or progress.get("percent") != 100:
                raise RuntimeError("instalação não confirmada; não repetir automaticamente")
            after = self.inspect()
            # minimum describes bytes used by installed images; installing
            # into a blank slot necessarily changes it without moving a partition.
            topology = lambda layout: {k:v for k,v in layout.items() if k != "minimum"}
            if topology(before["layout"]) != topology(after["layout"]):
                raise RuntimeError("layout alterado inesperadamente; inspecionar")
            for app in before["apps"]:
                if app["slot"] != slot and app not in after["apps"]:
                    raise RuntimeError("outro app mudou; inspecionar")
            if not any(a["slot"] == slot and a.get("name") == "tab5-emu" for a in after["apps"]):
                raise RuntimeError("descritor instalado não confirmado")
            # Ticket is not useful after execution and never included in saved report.
            staged["plan"] = {"message": message}
            return dict(staged, installed=True, booted=False, before=before, after=after)
        raise TimeoutError("resultado ambíguo; inspecionar antes de repetir")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inspect", "stage", "install", "update"))
    parser.add_argument("--port")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        raise FileExistsError(args.report)
    if args.action != "inspect" and args.image is None:
        parser.error("--image obrigatório")
    client = Launcher(args.port, os.getenv("PALUDO_URL"), os.getenv("PALUDO_TOKEN"))
    if args.action == "inspect":
        result = client.inspect()
    else:
        result = getattr(client, args.action)(args.image)
        if "plan" in result:
            result["plan"].pop("ticket", None)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("x") as output:
        json.dump(result, output, indent=2, ensure_ascii=False)
        output.write("\n")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
