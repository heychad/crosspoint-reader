"""Repeat snapshot fault test with read errors instead of write errors."""
from pathlib import Path
base=Path(__file__).with_name('probe-settings-snapshot-real-fs.py')
code=base.read_text()
changes={
 'for(bool stop:{false,true})':'for(bool stop:{false})',
 'disk.writes=0;disk.failAt=fault;disk.failStop=stop;':'disk.writes=0;disk.reads=0;disk.failRead=fault;',
 'if(fault>=0 && !disk.injected)break;if(disk.injected)++injections;':'if(fault>=0 && !disk.injectedRead)break;if(disk.injectedRead){++injections;if(result==settingsrecovery::CopyResult::Copied){std::fprintf(stderr,"hidden read failure offset=%d\\\\n",fault);return 7;}}',
 'disk.dead=false;disk.failAt=-1;FsVolume remount;':'disk.dead=false;disk.failRead=-1;FsVolume remount;',
 'injected write failures=%d source preserved':'injected read failures=%d source preserved',
}
for old,new in changes.items():
 assert code.count(old)==1,(old,code.count(old))
 code=code.replace(old,new)
exec(compile(code,str(base),'exec'),{'__file__':str(base),'__name__':'__main__'})
