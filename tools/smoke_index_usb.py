#!/usr/bin/env python3
"""macOS SD handover check; eject only the exact Tab5 USB device's disk."""
import plistlib, re, subprocess, sys, time


def tab5_disks():
    tree=plistlib.loads(subprocess.check_output(
        ['ioreg','-a','-l','-p','IOService','-r','-c','IOUSBHostDevice']))
    disks=set()
    def children(node):
        yield node
        for child in node.get('IORegistryEntryChildren',[]):yield from children(child)
    for root in tree:
        for node in children(root):
            if (node.get('idVendor'),node.get('idProduct'),node.get('USB Serial Number'))!=(0x1209,0x0d32,'tab5-emu'):continue
            for media in children(node):
                name=media.get('BSD Name','')
                if media.get('Whole') is True and re.fullmatch(r'disk[0-9]+',name):disks.add(name)
    return sorted(disks)


def check(c,report):
    from emulink.control import ControlError
    if sys.platform!='darwin':raise RuntimeError('this physical eject check uses macOS IORegistry')
    assert c.status()['state']=='menu' and c.settings()['library']['indexed']
    report['msc_before']=c.settings()['msc_io']
    assert not report['msc_before']['host_allowed']
    c.settings('library_page=78')  # cancel a real index worker job at handover
    c.usb_drive(True);report['host_owns_card']=True
    assert c.status()['usb_drive']
    report['msc_host']=c.settings()['msc_io']
    assert report['msc_host']['host_allowed']
    deadline=time.monotonic()+20;disks=[]
    while time.monotonic()<deadline:
        disks=tab5_disks()
        if len(disks)==1:break
        time.sleep(.5)
    if len(disks)!=1:
        raise RuntimeError('exact Tab5 whole disk not identified; SD remains with host; no firmware remount attempted')
    disk=disks[0];report['disk_identified_by_vid_pid_serial']=True
    for label,action in [('file',lambda:c.read_file('Tab5-Emu/library/games.json')),
                         ('launch',lambda:c.launch('tab5-diagnostic-ab627e01.gba'))]:
        try:action()
        except ControlError:report[label+'_refused_while_host_owns_card']=True
        else:raise AssertionError(label+' was allowed while host owns SD')
    for attempt in range(3):
        assert tab5_disks()==[disk], 'USB disk identity changed before eject'
        # macOS may spend more than 30 s stopping Spotlight on the volume.
        # A timed-out eject is ambiguous: do not remount the card in firmware.
        result=subprocess.run(['diskutil','eject','/dev/'+disk],capture_output=True,text=True,timeout=60)
        if result.returncode==0:break
        if 'mdsync' not in result.stderr:break
        time.sleep(3)
    if result.returncode:
        raise RuntimeError('OS eject failed; SD remains with host; no firmware remount attempted: '+result.stderr)
    report['os_eject_succeeded']=True
    report['os_eject_attempts']=attempt+1
    c.usb_drive(False);report['host_owns_card']=False
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        d=c.settings()
        assert not d['msc_io']['host_allowed']
        if not d['library_loading']:
            assert d['library']['indexed'] and d['library']['total']==5000 and not d['library']['error']
            assert len(c.roms())==64
            report['index_resumed_after_os_eject']=True
            report['msc_after']=d['msc_io']
            report['complete']=True
            return
        time.sleep(.2)
    raise TimeoutError('index did not resume after eject')
