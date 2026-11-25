"""Contrato SIMULADO; estes testes não acessam nem validam o aparelho."""
import hashlib
import sys
import tempfile
import unittest
import urllib.error
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from paludo import Console, Launcher
from package import check_image


class Fake(Launcher):
    def __init__(self, image, name="other-app", layout=False):
        self.image, self.name, self.layout = image, name, layout
        self.executed = False
    def inspect(self):
        name = "tab5-emu" if self.executed else self.name
        return {"apps": [{"slot": 1, "descriptorValid": bool(name), "name": name}],
                "layout": {"sizes": [4096], "minimum": [4096 if self.executed else 0]}}
    def api(self, route, data=None, raw=False):
        if route.startswith("/api/upload"):
            return {"verified": True}
        if route.startswith("/api/file"):
            return self.image.read_bytes()
        if route == "/api/operation" and data and data["action"] == "install":
            return {"ok": True, "ticket": "simulated", "message": "Instalar tab5-emu no slot 1 (app1). " +
                    ("Reorganiza a flash e reinicia." if self.layout else "Espaco disponivel; apps existentes preservados.")}
        if data and data["action"] == "execute":
            self.executed = True
            return {"ok": True}
        if route == "/api/operation" and data is None:
            return {"running":False,"percent":100,"error":0}
        raise AssertionError(route)


class ContractTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "synthetic-header.bin"
        data = bytearray(288)
        data[0] = 0xe9
        data[12] = 0x12
        data[23] = 1
        data[80:88] = b"tab5-emu"
        self.path.write_bytes(data + hashlib.sha256(data).digest())

    def test_large_upload_streams_fixed_length_without_chunked_encoding(self):
        import io
        payload=bytes(range(256))*40
        class Reply:
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def read(self):return b'{"verified":true}'
        class Opener:
            calls=0
            def open(self,request,timeout):
                self.calls+=1
                self.body=request.data
                self.assertions=(request.get_header('Content-length'),request.get_header('Transfer-encoding'))
                blocks=[]
                while block:=request.data.read(4096):blocks.append(block)
                self.received=b''.join(blocks)
                return Reply()
        client=Launcher.__new__(Launcher);client.url='http://127.0.0.1';client.token='simulated';client.opener=Opener()
        self.assertTrue(client.api('/api/upload',payload)['verified'])
        self.assertEqual(client.opener.received,payload)
        self.assertEqual(client.opener.assertions,(str(len(payload)),None))
        self.assertTrue(client.opener.body.closed)
        self.assertEqual(client.opener.calls,1)

    def test_upload_timeout_closes_stream_without_replaying_request(self):
        class Opener:
            calls=0
            def open(self,request,timeout):
                self.calls+=1;self.body=request.data
                raise TimeoutError('simulated send stall')
        client=Launcher.__new__(Launcher);client.url='http://127.0.0.1';client.token='simulated';client.opener=Opener()
        with self.assertRaises(TimeoutError):client.api('/api/upload',b'x'*8192)
        self.assertTrue(client.opener.body.closed);self.assertEqual(client.opener.calls,1)

    def test_digest_and_project_rejection(self):
        check_image(self.path)
        data = bytearray(self.path.read_bytes())
        data[90] ^= 1
        self.path.write_bytes(data)
        with self.assertRaises(ValueError):
            check_image(self.path)

    def test_console_retries_only_status_during_startup(self):
        class SerialStub:
            def __init__(self): self.sent=[]; self.reads=0
            def write(self,data): self.sent.append(data)
            def readline(self):
                self.reads+=1
                return b'' if self.reads==1 else (b'TAB5 {"ok":true,"version":"simulated"}\n' if self.sent[-1].startswith(b'tab5 "status"') else b'TAB5 {"ok":true,"message":"boot simulated"}\n')
        for command,expected_writes in [('status',2),('boot',1)]:
            console=Console.__new__(Console);console.serial=SerialStub()
            clock=iter(range(50))
            with patch('paludo.time.monotonic',side_effect=lambda:next(clock)):
                self.assertTrue(console.command(command)['ok'])
            self.assertEqual(len(console.serial.sent),expected_writes)

    def test_console_waits_for_usb_node_before_sending_any_command(self):
        import types
        class SerialStub:
            def __init__(self):self.opens=0;self.sent=[]
            def open(self):
                self.opens+=1
                if self.opens==1:raise OSError(2,'device node not ready')
            def write(self,data):self.sent.append(data)
            def reset_input_buffer(self):pass
        serial=SerialStub()
        module=types.SimpleNamespace(Serial=lambda **kwargs:serial,SerialException=OSError)
        with patch.dict(sys.modules,{'serial':module}),patch('paludo.time.sleep'):
            Console('simulated-port')
        self.assertEqual(serial.opens,2)
        self.assertEqual(serial.sent,[b'\r\n'])

    def test_console_ignores_late_status_before_pairing_reply(self):
        class SerialStub:
            def __init__(self):self.sent=[];self.replies=iter([b'TAB5 {"version":"simulated","error":0}\n',b'TAB5 {"ip":"127.0.0.1","code":"simulated","error":0}\n'])
            def write(self,data):self.sent.append(data)
            def readline(self):return next(self.replies)
        console=Console.__new__(Console);console.serial=SerialStub()
        self.assertEqual(console.command('web-start')['ip'],'127.0.0.1')
        self.assertEqual(len(console.serial.sent),1)

    def test_pairing_waits_for_wifi_startup_without_retrying_flash(self):
        replies=[RuntimeError({'error':259}),{'ip':'127.0.0.1','code':'simulated'}]
        class ConsoleStub:
            def command(self,command):
                self.assert_command=command
                value=replies.pop(0)
                if isinstance(value,Exception):raise value
                return value
        console=ConsoleStub()
        with patch('paludo.time.sleep'):
            self.assertEqual(Launcher.start_web_when_ready(console)['ip'],'127.0.0.1')
        self.assertEqual(console.assert_command,'web-start')
        self.assertEqual(replies,[])

    def test_stage_does_not_execute_flash(self):
        fake = Fake(self.path)
        report = fake.stage(self.path)
        self.assertTrue(report["received_hash_verified"])
        self.assertFalse(report["installed"])
        self.assertFalse(fake.executed)

    def test_progress_disconnect_does_not_repeat_execute(self):
        client=Launcher.__new__(Launcher)
        replies=[{'ok':True},ConnectionResetError('simulated progress disconnect'),
                 {'running':False,'percent':100,'error':0}]
        with patch.object(client,'api',side_effect=replies) as api,patch('paludo.time.sleep'):
            self.assertEqual(client.execute_plan({'ticket':'simulated'})['percent'],100)
        self.assertEqual(api.call_args_list[0].args,('/api/operation',{'action':'execute','ticket':'simulated'}))
        self.assertEqual([call.args for call in api.call_args_list[1:]],
                         [('/api/operation',),('/api/operation',)])

    def test_execute_disconnect_is_ambiguous_and_not_replayed(self):
        client=Launcher.__new__(Launcher)
        with patch.object(client,'api',side_effect=ConnectionResetError('simulated lost execute reply')) as api:
            with self.assertRaises(ConnectionResetError):client.execute_plan({'ticket':'simulated'})
        self.assertEqual(api.call_count,1)

    def test_wifi_reconnect_is_bounded_and_precedes_upload(self):
        commands=[]
        class ConsoleStub:
            def command(self,command):
                commands.append(command)
                if command=='web-start':raise RuntimeError({'error':259})
                if command=='wifi-retry':return {'error':0}
                raise AssertionError('unexpected mutation')
        ticks=iter([0,46,47,100])
        with patch('paludo.time.monotonic',side_effect=lambda:next(ticks)),patch('paludo.time.sleep'):
            with self.assertRaisesRegex(RuntimeError,'Nenhuma gravação iniciada'):
                Launcher.start_web_when_ready(ConsoleStub())
        self.assertEqual(commands,['web-start','wifi-retry','web-start'])

    def test_other_application_is_preserved(self):
        fake = Fake(self.path)
        with self.assertRaisesRegex(RuntimeError, "outro aplicativo"):
            fake.install(self.path)
        self.assertFalse(fake.executed)

    def test_reorganization_is_not_implicit(self):
        fake = Fake(self.path, name="tab5-emu", layout=True)
        with self.assertRaisesRegex(RuntimeError, "reorganização"):
            fake.install(self.path)
        self.assertFalse(fake.executed)

    def test_expired_pairing_renews_web_before_upload(self):
        commands = []
        class ConsoleStub:
            def __init__(self, port): pass
            def close(self): pass
            def command(self, command):
                commands.append(command)
                if command == "status": return {"version": "0.8.0-rc.2"}
                return {"ip": "127.0.0.1", "code": "simulated"}
        responses = [urllib.error.HTTPError("http://test/api/session", 401, "expired", {}, None), {"authenticated": True}]
        with patch("paludo.Console", ConsoleStub), patch.object(Launcher, "api", side_effect=responses) as api:
            Launcher(port="simulated-port")
        self.assertEqual(commands, ["status", "web-start", "web-stop", "web-start"])
        self.assertEqual(api.call_count, 2)

    def test_empty_slot_install_changes_usage_without_changing_layout(self):
        fake = Fake(self.path, name="")
        with patch("paludo.time.sleep"):
            result = fake.install(self.path)
        self.assertTrue(result["installed"])
        self.assertNotEqual(result["before"]["layout"]["minimum"], result["after"]["layout"]["minimum"])
        self.assertEqual(result["before"]["layout"]["sizes"], result["after"]["layout"]["sizes"])

    def test_owned_update_and_backup_failure(self):
        class Updating(Fake):
            def __init__(self,image,fail_backup=False,reservation=65536):
                super().__init__(image,name='tab5-emu')
                self.present=True; self.pending=''; self.actions=[]
                self.fail_backup=fail_backup;self.reservation=reservation
            def inspect(self):
                return {'apps':[{'slot':0,'name':'other','descriptorValid':True},
                    {'slot':1,'name':'tab5-emu' if self.present else '', 'descriptorValid':self.present}],
                    'layout':{'sizes':[65536,self.reservation],'minimum':[65536,self.reservation if self.present else 0],'launcher':2048}}
            def api(self,route,data=None,raw=False):
                if data and isinstance(data,dict) and route=='/api/operation':
                    action=data['action']; self.actions.append(action)
                    if action=='uninstall':
                        self.pending='uninstall'
                        assert data['backupBinary'] and not data['backupData'] and data['slot']==1
                        return {'ok':True,'ticket':'backup-remove','message':'Remover app 1 com backup do binario. Dados externos preservados.'}
                    if action=='install':self.pending='install'
                    if action=='execute':
                        if self.pending=='uninstall' and not self.fail_backup:self.present=False
                        if self.pending=='install':self.present=True
                        return {'ok':True}
                if route=='/api/operation' and data is None:
                    return {'running':False,'error':1 if self.fail_backup else 0,'percent':0 if self.fail_backup else 100}
                return super().api(route,data,raw)
        fake=Updating(self.path)
        with patch('paludo.time.sleep'): result=fake.update(self.path)
        self.assertTrue(result['installed'])
        self.assertEqual(fake.actions,['uninstall','execute','install','execute'])
        self.assertEqual(result['before']['layout']['sizes'],result['after']['layout']['sizes'])
        failed=Updating(self.path,fail_backup=True)
        with patch('paludo.time.sleep'),self.assertRaises(RuntimeError):failed.update(self.path)
        self.assertTrue(failed.present)
        self.assertNotIn('install',failed.actions)
        shrink=Updating(self.path,reservation=131072)
        with self.assertRaisesRegex(ValueError,'mudaria a reserva'):shrink.update(self.path)
        self.assertTrue(shrink.present)
        self.assertEqual(shrink.actions,[])
        too_small=Updating(self.path,reservation=128)
        with self.assertRaisesRegex(ValueError,'excede'):too_small.update(self.path)
        self.assertEqual(too_small.actions,[])


if __name__ == "__main__":
    unittest.main()
