# RetroRoom

A compliment to the StackSelector hardware specifically written for the ESP8266, but with the intention of supporting all the cheap microcontroller solutions.

The StackSelector is a piece of hardware that allows you to add an unlimited and easily expandable number of analog inputs to your TV, and then provides a beautiful user experience for selecting one of your many retro gaming consoles,  while attempting to hide all the ugly and annoying elements of dealing with input selections and formats. Soon to come: A youtube video!

## Overview of RetroRoom

This project is an exercise in getting many pieces of the puzzle to play well together. While I continue to prototype the StackSelector hardware, I've been working in parallel to ensure there are no roadblocks to the approach from the software and hardware integration side.

#### HTTP ⇔ HTTPS Bridge

The biggest risk factor for this portion of the project was controlling the hardware from a UI that could be hosted on a HTTPS connection while allowing HTTP communication to the microcontroller. I've managed this by hosting the parent with an iframe to the webUI (which is on an https server) and built a Message relay service that allows the two to communicate. Normally, http websockets communication is blocked from https, and for good reason, but the Message service in Javascript does not.

The reason for wanting the UI hosted on the web and not served locally was so I could update the UI for all devices at once, and have no limitations to the scope of interaction and experience on the web due to storage restrictions on the microcontroller.

## Completed

- [x] Wifi Support with basic UI for setting the preferred network
- [x] AsyncWebServer with easily included static files
- [x] Websockets for control and notification of machine state
- [x] Iframe with a Message relay host to allow a bridge from HTTPS to HTTP for simple UI hosting options
- [x] Rotary encoder to highlight the console you want to play
- [x] Selector switch via clicking the rotary encoder
- [x] Modified capacitive touch sensor which is attached to the ground of the rotary encode. Combined with a metal knob on the encoder, this allows the system to light LEDs as your hand nears or touches the knob.
- [x] FastLED library for LED control of strips as well as a ring of LEDS around the encoder, to show a rotation effect.
- [x] Add favicon.ico

## To Do

- [ ] Add the output controls for the StackSelector modules
- [ ] Add the IR controller library to select the input on the television automatically
- [ ] Add the input selection to the Console class
- [ ] Make the state save to whatever it is the wifi saves to, so it remembers your console selection
- [ ] Logo design for RetroRoom

## Feature Backlog
- [ ] make a more complete To Do list
- [ ] make the list of consoles editable from the web UI <- ooo
- [ ] add a secret mode using the easybutton onSequence for a light show
- [ ] see if we can remove the encoder tick call from the loop to purely use interupts
- [ ] break out the StackSelector module to it's own repo
