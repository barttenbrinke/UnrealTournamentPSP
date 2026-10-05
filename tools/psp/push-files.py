#!/usr/bin/env python3
"""push-files.py <list> -- copy files from the PSPLink host directory to the
Memory Stick, one at a time, through a running pspsh_drive.py session.

<list> holds paths relative to $PSP_HOST/UnrealTournament (e.g. Maps/DM-Codex.unr);
each goes to ms0:/PSP/GAME/UnrealTournament/<same path>. pspsh runs one
command at a time, but a long queue overflows the terminal's input buffer and
the tail is lost, so wait for each copy's prompt before sending the next.
About 150 KB/s over PSPLink.
"""
import os, sys, time
S = os.environ.get('PSP_WORK', os.path.expanduser('~/.ut-psp'))
FIFO, LOG = S + '/pspsh.in', S + '/pspsh.log'
DEST = 'ms0:/PSP/GAME/UnrealTournament/'

def send(cmd):
    with open(FIFO, 'w') as f:
        f.write(cmd + '\n')

def wait_prompt(after, marker, timeout):
    """Wait until the log, past offset `after`, holds `marker` and ends at a prompt."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(1)
        with open(LOG, 'rb') as f:
            f.seek(after)
            tail = f.read().decode('latin-1')
        if marker in tail and tail.rstrip().endswith('>'):
            return True
    return False

files = [l.strip() for l in open(sys.argv[1]) if l.strip()]
for d in sorted({os.path.dirname(f) for f in files}):
    send('mkdir ' + DEST + d)
    time.sleep(1)
for i, f in enumerate(files):
    start = os.path.getsize(LOG)
    size = os.path.getsize(os.path.join(S, 'psplink_host', 'UnrealTournament', f))
    send('cp host0:/UnrealTournament/%s %s%s' % (f, DEST, f))
    ok = wait_prompt(start, DEST + f, 60 + size / 50000)
    print('%3d/%d %s %s' % (i + 1, len(files), 'ok ' if ok else 'TIMEOUT', f), flush=True)
    if not ok:
        sys.exit(1)
