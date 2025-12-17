	1. First time working on a project of this size over an unfamiliar framework (NIXL). This caused me to spend 3-4 hrs bringing up a basic NIXL memory peer exchange! 
     Claude went off into a rabbit hole based on some example code using etcd. I finally restarted again with an even simpler first step which worked -- after which I successfully added complexity.
	Takeaway: Better to start AI assistant with the simplest/smallest code to implement which it can get right. This is a faster pattern than throwing a bigger problem at the assistant which it gets wrong & then spending long amount of time trying to debug & fix! I used this takeaway for the rest of the implementation & I (surprisingly) didn't get stuck anywhere (there were many bugs, but I could guide the Assistant easily).
	
	2. Claude commands for splitting context across multiple files & directories is buggy & does not match the documentation! My initial design in one .md was causing Claude to complain about context size. But breaking it into many files took way too long (2 hrs!) as I learnt that Claude did not work as documented!
	
	3. The in-memory protocol for this project is "stateful" by default. Numerous bugs related to this were found & fixed. My approach of incremental additions made debugging easy. 
     I could tell Claude the state of the test, share output, tell it what I suspected was the error. Claude was able to then focus & quickly find & fix the errors!

  4. I was able to do the entire repo management without once having to call any repo tools directly myself. This included setting up dependent subprojects
  5. Same with CMake (an area that one would normally waste hours on & still struggle to get right). All dependency tracking, linker/compiler flags etc were handled via few simple prompts.
  6. I was astounded at the ease with which I could create docker containers, the build environment around them and keeping them up to date. Moreover, creating complex cluster network topologies with traffic shaping was also handled relatively seamlessly! (again something one would spend an inordinate amount of time on)

  7. And I have not even mentioned the coding!! :-)
