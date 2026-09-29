
legend: * = fully lit led
		o = dimly lit led
		p = pulsing led
		- = unlit led not over a console
		0 = unlit led over a console

console 1 enabled:
***----------0000-------0000000-------000

as the knob is rotated through the first 4 detents:
***----------0000-------0000000-------000
***oo--------0000-------0000000-------000
***ooooo-----0000-------0000000-------000
***ooooooo---0000-------0000000-------000
***oooooooooo0000-------0000000-------000

regardless of the space and led between each console, the dimly lit led "knob turn progression indicator" should smoothly animate across the gap. so, if i do one detent, the first two leds (in this example) would animate / fade into their dimly enabled state.

on the fifth detent, we trigger the animation, the sequence would go something like:
***oooooooooo0000-------0000000-------000
oo**ooooooooo0000-------0000000-------000
ooo****oooooo0000-------0000000-------000
ooo--******oo0000-------0000000-------000
ooo-----******000-------0000000-------000
ooo-------******0-------0000000-------000
ooo--------******-------0000000-------000
ooo---------*****-------0000000-------000
ooo----------pppp-------0000000-------000

I chose the number of "blank spots" in this to be divisible by 10 so we can convert the example to math. I propose that we antialias the animation as well, so we'd generate the "onstate" with that that would antialias the output to each LED when going from segmwnt to segment. Note how the current console is left dimly light too. it would fade out when the new one is selected.


ideal wiring diagram for multiple rows:

***----------0000-------0000000-------000  >-wire out--|
co1__________con2_______consol3_______co4              |
--top shelf------------------------------              |
													   |
---0000--------0000------000000------000-  <-wire in---|
___con5________con6______conso7______co8-
--middle shelf---------------------------

the animation from console4 (co4) to console5 (con5) would
need to go across the middle shelf

the math for this is difficult and likely error prone. if need be, 
we can wire like this instead:

   ***----------0000-------0000000-------000  >-wire out--|
    													  |
-------------<--------------------------------<-----------|
|
in>---0000--------0000------000000------000-  