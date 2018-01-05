# mousebang
Mouse latency tester

So this is an embarassingly badly coded hack I did so mouse button latency
can be tested for two different mice, even when using the same button (i.e.
test LMB vs LMB).

Its UI is bad (but once done it lets you copy the data to clipboard) and it's
frankly embarrassing how bad Win32/Win64 programming becomes when you have
no discipline.  I made no effort at keeping the code that well factored
TBH and it's not an indication of my normal code quality. It was just thrown
together so we could argue about how bad some no-name mice are.

This is licensed using the GPL v2 or at your option a later version.  But
frankly I'm not sure why you'd want to use this code.  If you do want to use
it in a different license please contact me, I might be flexible.

No warranties etc.  For one it might hang you Windows input queue and force
you to reboot.  I tried to keep it well-behaved.
