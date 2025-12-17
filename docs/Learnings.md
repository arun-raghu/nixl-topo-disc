First time working on a project of this size over an unfamiliar framework (NIXL). This caused me to spend 3-4 hrs bringing 
up a basic NIXL memory peer exchange! Claude went off into a rabbit hole based on some example code using etcd. I finally 
restarted again with an even simpler first step which worked -- after which I successfully added complexity.
	
Takeaway: Better to start AI assistant with the simplest/smallest code to implement which it can get right. This is a faster 
	pattern than throwing a bigger problem at the assistant which it gets wrong & then spending long amount of time trying to 
	debug & fix! I used this takeaway for the rest of the implementation & I (surprisingly) didn't get stuck anywhere (there 
	were many bugs, but I could guide the Assistant easily).
	
Claude commands for splitting context across multiple files & directories is buggy & does not match the documentation! 
My initial design in one .md was causing Claude to complain about context size. But breaking it into many files took way 
too long (2 hrs!) as I learnt that Claude did not work as documented!

The in-memory protocol for this project is "stateful" by default. Numerous bugs related to this were found & fixed. My 
approach of incremental additions made debugging easy. I could tell Claude the state of the test, share output, tell it 
what I suspected was the error. Claude was able to then focus & quickly find & fix the errors!

I hadn't used Claude with docker earlier and I was surprised at the ease with which I could create docker images, 
the build environment around them and keeping them up to date. Moreover, creating complex cluster network topologies 
with traffic shaping was also handled relatively seamlessly! 
