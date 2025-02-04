# OMX-27 Documentation

## Concepts

OMX-27 is a MIDI Keyboard and Sequencer. Both USBMIDI and hardware MIDI (via 1.8" TRS jack) are supported. Various "modes" can be accessed with the encoder and specific functions, parameters or sub-modes can be accessed with the encoder or key-presses/key-combinations.

Sequencer modes have 8 patterns (tracks). Sequencer modes currently send MIDI clock and transport control (start/stop) by default.

CV pitch output is limited to about 4.3 octaves.

USBMIDI should be plug-and-play with any USBMIDI compatible host. iPad works great with the camera connection kit. Hardware MIDI TRS jack is switchable between Type-A and Type-B.


## Layout

<img src="omx27_layout3.png" alt="omx27_layout3" width="1000" height="402" />

Top left key = AUX Key.

Potentiometers are mapped by default to CCs 21, 22, 23, 24 and 7 (volume). These can be changed in the firmware. Hopefully this will be configurable from a device menu or a web-MIDI interface in future.

### Encoder

Long press encoder to enable mode change. Turn to change modes, short-press to enter mode.

Within the modes a short press on the encoder will select a parameter to edit.


### Key Switches

In MIDI modes the key switches work as a normal MIDI keyboard.

In sequencer modes the switches are broken into groups - Function Keys, Pattern Keys and Step Keys.


__"Black keys" (sharp/flat keyboard keys)__

The first 2 black keys are Function Keys (FUNC)
- F1
- F2

The next 8 are Pattern Keys and they select the active sequence pattern (P1-P8).

Hold a key (long press) to access parameters for that pattern. This is "Pattern Params".


__"White keys" (bottom row)__

Sequencer Step Keys - These are your sequencer step on/off keys.

Hold a key (long press) to access parameters for that step. This is "Note Select / Step Parameters". F1 + Step Key is also a quick shortcut.


---

## Modes

Long-press the encoder to enter Mode Select. Short-press to enter that mode.

While in Mode Select (before you short-press), you can save the state of the sequencer and settings to memory by hitting the AUX key.

### MI - MIDI

MIDI Keyboard. Encoder selects octave or channel.

AUX does nothing here (yet)

### S1 - Sequencer 1

Step sequencer - One pattern active at a time.

 - AUX is Start/Stop
 - Start/Stop sends MIDI transport control, and MIDI clock when running
 - Pattern Key: Selects playing pattern
 - F1 + AUX: Reset sequences to first/last step
 - F2 + AUX: Reverse pattern direction
 - F1 + Pattern Key: Enter "Step Record Mode" (transport must be stopped)
 - F2 + Pattern Key: Mute that pattern
 - Long press a Step Key: Enter "Note Select Mode"
 - Long press a Pattern Key: Enter "Pattern Params Mode"
 - AUX-key exits sub-modes

### S2 - Sequencer 2

Step sequencer - All patterns active.

 - AUX is Start/Stop
 - Start/Stop sends MIDI transport control, and MIDI clock when running
 - Pattern Key: Selects active pattern
 - Encoder changes "page" for sequence parameters (with no parameter highlighted)
 - Short-press encoder to highlight active parameter to edit
 - F1 + AUX: Reset sequences to first/last step
 - F2 + AUX: Reverse pattern direction
 - F1 + Pattern Key: Enter "Step Record Mode"
 - F2 + Pattern Key: Mute that pattern
 - Long press a Step Key: Enter "Note Select Mode"
 - Long press a Pattern Key: Enter "Pattern Params Mode"
 - AUX-key exits sub-modes

In the sequencer modes, the default setup is a GM Drum Map with each pattern on a consecutive midi channel. So that's notes 36, 38, 37, 39, 42, 46, 49, 51 on channels 1-8.

### OM - Organelle Mother

Pretty much the same as MI, but with the following tweaks for Organelle Mother on norns/fates/raspberry-pi.

- AUX key sends CC 25 (127 on press, 0 on release)
- Encoder turn sends CC 28 (127 on CW, 0 on CCW)


## Sub-Modes

AUX-key exits sub-modes

### Note Select / Step Parameters

Long press a step key to enter this mode. Here you can change the note values (note number, velocity, note length and octave), set CC parameter values with the knobs, and set step parameters (step events, step probability, trig conditions).

Step Events:
"-" mute
"+" play
"1" reset to first step
">>" set parttern direction forward
"<<" set parttern direction reverse
"#?" jump to random step number
"?" set random event (of any of the previous events) for that one step

Trig conditions - A/B Ratios:
Play that step on the A cycle of B total cycles (or bars) of the pattern. Default is 1:1 (every time).
First number - play step on that cycle thru the pattern
Second number - resets the counter after that pattern cycle.

So 1:4 would play on the first cycle, not play on the next three and then reset (after the 4th cycle). 3:8 would play only the 3rd cycle and reset after the 8th.


### Step Record

(SH-101-ish style note entry)

Holding F1 + a Pattern Key will enter Step Record Mode.

Enter notes from the keyboard and the sequence step will automatically advance to the next step. Change knob 1-4 positions to set a CC parameter lock for that step. Knob #5 (far right) will enter a velocity value for that step (there is no visual feedback when entering values from the knobs.

You can short-press the encoder button to skip a step (making no note change). However, you can still record plocks/velocity for that step with the knobs. A nice trick might be to make one pass through playing notes on the keyboard and then make a second pass using the encoder button to add plocks or velocity values for each step.

In this sub-mode, turning the encoder is locked to change octave. The display will show you the current pattern, step, note-value, and octave.

Use the AUX to exit this sub-mode.


### Pattern Params

Long press Pattern Key to enter Pattern Params Mode.

Turning the encoder will show different pages of parameters.

A short-press on the encoder will select the active parameter for editing. Press the encoder repeatedly until nothing is selected to change pages.

Page 1: Pattern, pattern length, rotation, midi channel

Page 2 (see Sequence Reset Automation below): Start, end, frequency, probability

Page 3: Rate (sixteenth notes, eight notes, etc.), MIDI solo.


Step Keys also set pattern length
F1 + pattern will copy pattern
F2 + pattern will paste pattern to other pattern slot
F1 + F2 + pattern will clear the pattern back to GM drum map default (and clear all plocks)

(you can paste multiple times - paste buffer should stay the same until you copy again)

MIDI solo:
Set a pattern to MIDI solo and you can play the keyboard while that pattern is selected.

Note - once in MIDI solo, you will only be able to change the active pattern by using the encoder knob.

### Pattern Params: Sequence Reset Automation

This is located on the second page of pattern parameters

The goal of this "Sequence Reset Automation" feature was developed in the spirit of classic sequencers that can generate more complex sequences from simpler ones by setting any step in a given sequence to trigger a "reset" based on some constraint (i.e., number of cycles, probability, random).

Note - This behavior is a pattern-based solution. You can also execute step-based resets in Step Parameters.

Settings:

- START (Currently 0 - PatternLength-1): Use this to set the start step in current pattern to reset to for beginning a new cycle.

- END (Currently 0 - PatternLength-1): Use this to set the last step in current sequence to end/reset pattern cycles. This in essence is the step that will be used to trigger resets.

- FREQ of trigger reset (i.e., every X sequence cycle iterations)

- PROB of triggering reset (percentage)

NOTE: Setting STEP = 0 and PROB = 1 dictates random trigger steps which can lead to interesting results by jumping to random position/step.



# MIDI Switch for the mini TRS jack connection

A hardware switch on the device will let you swap between Type-A and Type-B for the hardware MIDI TRS jack.


Products That Use Type-B mini TRS Jack Connections
- Arturia BeatStep Pro
- Novation products
- Polyend products
- 1010music Original Series 1 modules, Series 2 modules, Blackbox, MX4 and Euroshield

Products That Use Type-A mini TRS Jack Connections
- ADDAC System products
- Arturia BeatStep (not to be confused with the BeatStep Pro)
- IK Multimedia products
- inMusic (Akai) products
- Korg products
- Line 6 products
- little Bits w5 MIDI module
- Make Noise 0-Coast

See https://1010music.com/stereo-minijacks-midi-connections-compatibility-guide for more information

