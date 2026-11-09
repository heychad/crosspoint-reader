"""Exercise the actual settings page save handler after ambiguous failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / 'src/network/html/SettingsPage.html').read_text()
a = text.index('  let settingsSaveBlocked = false;')
b = text.index('\n  function clearPassword', a)
mark = text[a:b]
a = text.index('  async function saveSettings()')
b = text.index('\n  // --- Wi-Fi Network Management ---', a)
save = text[a:b]
a = text.index('  async function loadSettings()')
b = text.index('\n  async function saveSettings()', a)
load = text[a:b]
fixture = r'''
const assert = require('node:assert/strict');
const button = {disabled:false, textContent:''};
const help = {hidden:true, textContent:''};
const document = {getElementById: id => id === 'settings-save-help' ? help : button};
const allSettings = [{key:'quickA'}, {key:'quickB'}];
const originalValues = {quickA:0, quickB:0};
let settingsReloadHelp = 'Localized reload instruction';
const getValue = () => 1;
let calls = 0, messages = [];
const showWebToast = (message, error) => messages.push({message,error});
let fetch = async () => {++calls; return {ok:false,text:async()=> 'save failed'};};
'''
body = r'''
(async () => {
 await saveSettings(); assert.equal(calls,1); assert.equal(button.disabled,true);
 assert.equal(button.textContent,'Save Settings'); assert.equal(messages[0].error,true);
 assert.ok(messages[0].message.includes(settingsReloadHelp));
 assert.equal(help.hidden,false);assert.equal(help.textContent,settingsReloadHelp);
 markChanged(); assert.equal(button.disabled,true); await saveSettings(); assert.equal(calls,1);
 assert.deepEqual(originalValues,{quickA:0,quickB:0});
 settingsSaveBlocked=false;messages=[];button.disabled=false;help.hidden=true;help.textContent='';
 fetch=async()=>{++calls;throw new Error('network unavailable');};
 await saveSettings();assert.equal(calls,2);assert.equal(button.disabled,true);
 assert.ok(messages[0].message.includes(settingsReloadHelp));
 assert.equal(help.hidden,false);assert.equal(help.textContent,settingsReloadHelp);
 markChanged();await saveSettings();assert.equal(calls,2);
 settingsSaveBlocked=false;help.hidden=true;settingsReloadHelp='';await saveSettings();
 assert.equal(calls,3);assert.equal(help.hidden,false);assert.equal(help.textContent,'Reload this page before trying again.');
})().catch(e => { console.error(e); process.exitCode=1; });
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'test.js'
    path.write_text(fixture + mark + save + body)
    result = subprocess.run(['node', str(path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    load_fixture = r'''
const assert=require('node:assert/strict');
let allSettings=[],originalValues={},settingsReloadHelp='',preserveQuickResumeTimeoutOn=false,quickResumeTimeoutAutoEnabled=false;
const nodes={'settings-container':{innerHTML:''},'save-container':{style:{}},'saveBtn':{disabled:false}};
const document={getElementById:id=>nodes[id]};
const escapeHtml=s=>s, renderControl=()=>'', updateSettingsVisibility=()=>{}, getControlValue=()=>0;
const syncQuickResumeTimeoutForSleepScreen=()=>false, markChanged=()=>{};
const seen=[];
let header=encodeURIComponent('Localized reload');
const fetch=async url=>{seen.push(url);return {ok:true,headers:{get:()=>header},json:async()=>[{key:'a',value:1,category:'Reader',name:'A'}]};};
'''
    load_body = r'''
(async()=>{await loadSettings();assert.deepEqual(seen,['/api/settings']);assert.equal(settingsReloadHelp,'Localized reload');assert.equal(originalValues.a,1);assert.equal(nodes.saveBtn.disabled,true);header=null;await loadSettings();assert.equal(settingsReloadHelp,'');assert.equal(originalValues.a,1);})().catch(e=>{console.error(e);process.exitCode=1;});
'''
    path.write_text(load_fixture + load + load_body)
    result = subprocess.run(['node', str(path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
print('Failed web save blocks replay of stale settings')
