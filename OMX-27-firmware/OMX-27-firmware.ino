// OMX-27 MIDI KEYBOARD / SEQUENCER
// v 1.4.0
//
// Steven Noreyko, May 2021
//
//
//	Big thanks to:
//	John Park and Gerald Stevens for initial testing and feature ideas
//	mzero for immense amounts of code coaching/assistance
//	drjohn for support
//  Additional code contributions: Matt Boone, Steven Zydek, Chris Atkins, Will Winder


#include <Adafruit_Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <ResponsiveAnalogRead.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "consts.h"
#include "config.h"
#include "colors.h"
#include "MM.h"
#include "ClearUI.h"
#include "sequencer.h"
#include "noteoffs.h"
#include "storage.h"


U8G2_FOR_ADAFRUIT_GFX u8g2_display;

const int potCount = 5;
ResponsiveAnalogRead *analog[potCount];

// storage of pot values; current is in the main loop; last value is for midi output
int volatile currentValue[potCount];
int lastMidiValue[potCount];
int potMin = 0;
int potMax = 8190;
int temp;

// Timers and such
elapsedMillis blink_msec = 0;
elapsedMillis slow_blink_msec = 0;
elapsedMillis pots_msec = 0;
elapsedMillis dialogTimeout = 0;
elapsedMillis dirtyDisplayTimer = 0;
unsigned long displayRefreshRate = 60;
elapsedMicros clksTimer = 0;		// is this still in use?

//unsigned long clksDelay;
elapsedMillis keyPressTime[27] = {0};

using Micros = unsigned long;
Micros lastProcessTime;
Micros nextStepTime;
Micros lastStepTime;
volatile unsigned long step_micros;
volatile unsigned long noteon_micros;
volatile unsigned long noteoff_micros;
volatile unsigned long ppqInterval;

// ANALOGS
int analogValues[] = {0,0,0,0,0};		// default values
int potValues[] = {0,0,0,0,0};
int potCC = pots[0];
int potVal = analogValues[0];
int potNum = 0;
bool plockDirty[] = {false,false,false,false,false};
int prevPlock[] = {0,0,0,0,0};

// MODES
OMXMode omxMode = DEFAULT_MODE;
OMXMode newmode = DEFAULT_MODE;

int nspage = 0;
int pppage = 0;
int sqpage = 0;
int srpage = 0;

int patmode = 0;
int nsmode = 4;
int nsmode2 = 4;
int nsmode3 = 4;
int ppmode = 4;
int ppmode2 = 4;
int ppmode3 = 4;
int mimode = 4;
int sqmode = 4;
int sqmode2 = 4;
int srmode = 4;
int srmode2 = 4;
int modehilight = 4;

// VARIABLES / FLAGS
float step_delay;
bool dirtyPixels = false;
bool dirtyDisplay = false;
bool blinkState = false;
bool slowBlinkState = false;
bool noteSelect = false;
bool noteSelection = false;
bool patternParams = false;
int noteSelectPage = 0;
int selectedNote = 0;
int selectedStep = 0;
bool stepSelect = false;
bool stepRecord = false;
bool stepDirty = false;
bool dialogFlags[] = {false, false, false, false, false, false};
unsigned dialogDuration = 1000;

bool copiedFlag = false;
bool pastedFlag = false;
bool clearedFlag = false;

bool enc_edit = false;
bool midiAUX = false;

int defaultVelocity = 100;
int octave = 0;			// default C4 is 0 - range is -4 to +5
int newoctave = octave;
int transpose = 0;
int rotationAmt = 0;
int hline = 8;
int pitchCV;
uint8_t RES;
uint16_t AMAX;
int V_scale;

// clock
float clockbpm = 120;
float newtempo = clockbpm;
unsigned long tempoStartTime, tempoEndTime;

unsigned long blinkInterval = clockbpm * 2;
unsigned long longPressInterval = 1500;

uint8_t swing = 0;
const int maxswing = 100;
// int swing_values[maxswing] = {0, 1, 3, 5, 52, 66, 70, 72, 80, 99 }; // 0 = off, <50 early swing , >50 late swing, 99=drunken swing

bool keyState[27] = {false};
int midiKeyState[27] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};


// ENCODER
Encoder myEncoder(12, 11); 	// encoder pins on hardware
Button encButton(0);		// encoder button pin on hardware
//long newPosition = 0;
//long oldPosition = -999;


//initialize an instance of class Keypad
Adafruit_Keypad customKeypad = Adafruit_Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Declare NeoPixel strip object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// setup EEPROM/FRAM storage
Storage* storage;

// ####### CLOCK/TIMING #######

void advanceClock(Micros advance) {
	static Micros timeToNextClock = 0;
	while (advance >= timeToNextClock) {
		advance -= timeToNextClock;

		MM::sendClock();
		timeToNextClock = ppqInterval * (PPQ / 24);
	}
	timeToNextClock -= advance;
}

void advanceSteps(Micros advance) {
	static Micros timeToNextStep = 0;
//	static Micros stepnow = micros();
	while (advance >= timeToNextStep) {
		advance -= timeToNextStep;
		timeToNextStep = ppqInterval;

		// turn off any expiring notes
		pendingNoteOffs.play(micros());

		// turn on any pending notes
		pendingNoteOns.play(micros());
	}
	timeToNextStep -= advance;
}

void resetClocks(){
	// BPM tempo to step_delay calculation
	ppqInterval = 60000000/(PPQ * clockbpm); 		// ppq interval is in microseconds
	step_micros = ppqInterval * (PPQ/4); 			// 16th note step in microseconds (quarter of quarter note)

	// 16th note step length in milliseconds
	step_delay = step_micros * 0.001; 	// ppqInterval * 0.006; // 60000 / clockbpm / 4;
}

void setGlobalSwing(int swng_amt){
	for(int z=0; z<NUM_PATTERNS; z++) {
		patternSettings[z].swing = swng_amt;
	}
}

// ####### POTENTIMETERS #######

void sendPots(int val, int channel){
	MM::sendControlChange(pots[val], analogValues[val], channel);
	potCC = pots[val];
	potVal = analogValues[val];
	potValues[val] = potVal;
}

void readPotentimeters(){
	for(int k=0; k<potCount; k++) {
		temp = analogRead(analogPins[k]);
		analog[k]->update(temp);

			// read from the smoother, constrain (to account for tolerances), and map it
			temp = analog[k]->getValue();
			temp = constrain(temp, potMin, potMax);
		temp = map(temp, potMin, potMax, 0, 16383);

		// map and update the value
			analogValues[k] = temp >> 7;

			if(analog[k]->hasChanged()) {
			 		// do stuff
			switch(omxMode) {
				case MODE_OM:
						// fall through - same as MIDI
				case MODE_MIDI: // MIDI
					sendPots(k, midiChannel);
					dirtyDisplay = true;
					break;

				case MODE_S2: // SEQ2
						// fall through - same as SEQ1
				case MODE_S1: // SEQ1
					if (noteSelect && noteSelection){ // note selection - do P-Locks
						potNum = k;
						potCC = pots[k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[playingPattern][selectedStep].params[k] = analogValues[k];
							sendPots(k, PatternChannel(playingPattern));
						}
						sendPots(k, PatternChannel(playingPattern));
						dirtyDisplay = true;

					} else if (stepRecord){
						potNum = k;
						potCC = pots[k];
						potVal = analogValues[k];

						if (k < 4){ // only store p-lock value for first 4 knobs
							stepNoteP[playingPattern][seqPos[playingPattern]].params[k] = analogValues[k];
							sendPots(k, PatternChannel(playingPattern));
						} else if (k == 4){
							stepNoteP[playingPattern][seqPos[playingPattern]].vel = analogValues[k]; // SET POT 5 to NOTE VELOCITY HERE
						}
						dirtyDisplay = true;
					} else if (!noteSelect || !stepRecord){
						sendPots(k, PatternChannel(playingPattern));
					}
					break;

				default:
					break;
				}
			}
	}
}



// ####### SETUP #######

void setup() {
	Serial.begin(115200);

	storage = Storage::initStorage();
	dialogTimeout = 0;
	clksTimer = 0;

	lastProcessTime = micros();
	resetClocks();

	nextStepTime = micros();
	lastStepTime = micros();
	for (int x=0; x<NUM_PATTERNS; x++){
		timePerPattern[x].nextStepTimeP = nextStepTime; // initialize all patterns
		timePerPattern[x].lastStepTimeP = lastStepTime; // initialize all patterns
		patternSettings[x].clockDivMultP = 2; // set all DivMult to 2 for now
	}
	randomSeed(analogRead(13));

	// SET ANALOG READ resolution to teensy's 13 usable bits
	analogReadResolution(13);

	// initialize ResponsiveAnalogRead
	for (int i = 0; i < potCount; i++){
		analog[i] = new ResponsiveAnalogRead(0, true, .001);
		analog[i]->setAnalogResolution(1 << 13);

		// ResponsiveAnalogRead is designed for 10-bit ADCs
		// meanining its threshold defaults to 4. Let's bump that for
		// our 13-bit adc by setting it to 4 << (13-10)
		analog[i]->setActivityThreshold(32);

		currentValue[i] = 0;
		lastMidiValue[i] = 0;
	}

	// HW MIDI
	MM::begin();

	//CV gate pin
	pinMode(CVGATE_PIN, OUTPUT);

		// set DAC Resolution CV/GATE
		RES = 12;
		analogWriteResolution(RES); // set resolution for DAC
			AMAX = pow(2,RES);
			V_scale = 64; // pow(2,(RES-7)); 4095 max
		analogWrite(CVPITCH_PIN, 0);

	// Load from EEPROM
	bool bLoaded = loadFromStorage();
	if ( !bLoaded )
	{
		// Failed to load due to initialized EEPROM or version mismatch
		// defaults
		omxMode = DEFAULT_MODE;
		playingPattern = 0;
		midiChannel = 1;
		pots[0] = CC1;
		pots[1] = CC2;
		pots[2] = CC3;
		pots[3] = CC4;
		pots[4] = CC5;
		initPatterns();
	}

		// Init Display
	initializeDisplay();
	u8g2_display.begin(display);

	// Startup screen
	display.clearDisplay();
	testdrawrect();
	delay(200);
	display.clearDisplay();
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);
	drawLoading();

	// Keypad
	customKeypad.begin();

	//LEDs
	strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
	strip.show();            // Turn OFF all pixels ASAP
	strip.setBrightness(LED_BRIGHTNESS); // Set BRIGHTNESS to about 1/5 (max = 255)
	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, HALFWHITE);
		strip.show();   // Send the updated pixel colors to the hardware.
		delay(5); // Pause before next pass through loop
	}
	rainbow(5); // rainbow startup pattern
	delay(500);

	// clear LEDs
	strip.fill(0, 0, LED_COUNT);
	strip.show();

	delay(100);

	// Clear display
	display.display();

	dirtyDisplay = true;
}

// ####### END SETUP #######



// ####### MIDI LEDS #######

void midi_leds() {
	if (midiAUX){
		strip.setPixelColor(0, MEDRED);
	} else {
		strip.setPixelColor(0, LEDOFF);
	}
	dirtyPixels = true;
}

// ####### SEQUENCER LEDS #######

void show_current_step(int patternNum) {
	blinkInterval = step_delay*2;
	unsigned long slowBlinkInterval = blinkInterval * 2;

	if (blink_msec >= blinkInterval){
		blinkState = !blinkState;
		blink_msec = 0;
	}
	if (slow_blink_msec >= slowBlinkInterval){
		slowBlinkState = !slowBlinkState;
		slow_blink_msec = 0;
	}


	// AUX KEY

	if (playing && blinkState){
		strip.setPixelColor(0, WHITE);
	} else if (noteSelect && blinkState){
		strip.setPixelColor(0, NOTESEL);
	} else if (patternParams && blinkState){
		strip.setPixelColor(0, seqColors[patternNum]);
	} else if (stepRecord && blinkState){
		strip.setPixelColor(0, seqColors[patternNum]);
	} else {
		switch(omxMode){
			case MODE_S1:
				strip.setPixelColor(0, SEQ1C);
				break;
			case MODE_S2:
				strip.setPixelColor(0, SEQ2C);
				break;
			default:
				strip.setPixelColor(0, LEDOFF);
				break;
		}
	}

	if (patternSettings[patternNum].mute) {
		stepColor = muteColors[patternNum];
	} else {
		stepColor = seqColors[patternNum];
		muteColor = muteColors[patternNum];
	}

	if (noteSelect && noteSelection) {
		for(int j = 1; j < NUM_STEPS+11; j++){
			if (j < PatternLength(patternNum)+11){
				if (j == selectedNote){
					strip.setPixelColor(j, HALFWHITE);
				} else if (j == selectedStep+11){
					strip.setPixelColor(j, SEQSTEP);
				} else{
					strip.setPixelColor(j, LEDOFF);
				}
			} else {
				strip.setPixelColor(j, LEDOFF);
			}
		}

	} else if (stepRecord) {
		for(int j = 1; j < NUM_STEPS+11; j++){
			if (j < PatternLength(patternNum)+11){
				if (j == seqPos[playingPattern]+11){
					strip.setPixelColor(j, SEQCHASE);
//				} else if (j == selectedNote){
//					strip.setPixelColor(j, HALFWHITE);
				} else if (j != selectedNote){
					strip.setPixelColor(j, LEDOFF);
				}
			} else  {
				strip.setPixelColor(j, LEDOFF);
			}
		}
	} else if (patternSettings[playingPattern].solo){
//		for(int i = 0; i < NUM_STEPS; i++){
//			if (i == seqPos[patternNum]){
//				if (playing){
//					strip.setPixelColor(i+11, SEQCHASE); // step chase
//				} else {
//					strip.setPixelColor(i+11, LEDOFF);  // DO WE NEED TO MARK PLAYHEAD WHEN STOPPED?
//				}
//			} else {
//				strip.setPixelColor(i+11, LEDOFF);
//			}
//		}
	} else {
		for(int j = 1; j < NUM_STEPS+11; j++){
			if (j < PatternLength(patternNum)+11){
				if (j == 1) {

					// NOTE SELECT
					if (keyState[j] && blinkState){
						strip.setPixelColor(j, LEDOFF);
					} else {
						strip.setPixelColor(j, FUNKONE);
					}
				} else if (j == 2) {

					// PATTERN PARAMS
					if (keyState[j] && blinkState){
						strip.setPixelColor(j, LEDOFF);
					} else {
						strip.setPixelColor(j, FUNKTWO);
					}

				} else if (j == patternNum+3){  			// PATTERN SELECT
					strip.setPixelColor(j, stepColor);
					if (patternParams && blinkState){
						strip.setPixelColor(j, LEDOFF);
					}
				} else {
					strip.setPixelColor(j, LEDOFF);
				}
			} else {
				strip.setPixelColor(j, LEDOFF);
			}
		}

		for(int i = 0; i < NUM_STEPS; i++){
			if (i < PatternLength(patternNum)){
				if (patternParams){
 					strip.setPixelColor(i+11, SEQMARKER);
 				}

				if(i % 4 == 0){ // mark groups of 4
					if(i == seqPos[patternNum]){
						if (playing){
							strip.setPixelColor(i+11, SEQCHASE); // step chase
						} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
							if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
								if (slowBlinkState){
									strip.setPixelColor(i+11, stepColor); // step event color
								}else{
									strip.setPixelColor(i+11, muteColor); // step event color
								}
							} else {
								strip.setPixelColor(i+11, stepColor); // step on color
							}
						} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
							strip.setPixelColor(i+11, SEQMARKER);
						}

					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
						if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
							if (slowBlinkState){
								strip.setPixelColor(i+11, stepColor); // step event color
							}else{
								strip.setPixelColor(i+11, muteColor); // step event color
							}
						} else {
							strip.setPixelColor(i+11, stepColor); // step on color
						}
					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
						strip.setPixelColor(i+11, SEQMARKER);
					}

				} else if (i == seqPos[patternNum]){ 	// step chase
					if (playing){
						strip.setPixelColor(i+11, SEQCHASE);

					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
						if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
							if (slowBlinkState){
								strip.setPixelColor(i+11, stepColor); // step event color
							}else{
								strip.setPixelColor(i+11, muteColor); // step event color
							}
						} else {
							strip.setPixelColor(i+11, stepColor); // step on color
						}
					} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
						strip.setPixelColor(i+11, LEDOFF);  // DO WE NEED TO MARK PLAYHEAD WHEN STOPPED?
					}

				} else if (stepNoteP[patternNum][i].trig == TRIGTYPE_PLAY){
					if (stepNoteP[patternNum][i].stepType != STEPTYPE_NONE){
						if (slowBlinkState){
							strip.setPixelColor(i+11, stepColor); // step event color
						}else{
							strip.setPixelColor(i+11, muteColor); // step event color
						}
					} else {
						strip.setPixelColor(i+11, stepColor); // step on color
					}

				} else if (!patternParams && stepNoteP[patternNum][i].trig == TRIGTYPE_MUTE){
					strip.setPixelColor(i+11, LEDOFF);
				}

			}
		}
	}
	dirtyPixels = true;
//	strip.show();
}
// ####### END LEDS




// ####### DISPLAY FUNCTIONS #######

void dispGridBoxes(){
	display.fillRect(0, 0, gridw, 10, WHITE);
	display.drawFastVLine(gridw/4, 0, gridh, INVERSE);
	display.drawFastVLine(gridw/2, 0, gridh, INVERSE);
	display.drawFastVLine(gridw*0.75, 0, gridh, INVERSE);
}
void invertColor(bool flip){
	if (flip) {
		u8g2_display.setForegroundColor(BLACK);
		u8g2_display.setBackgroundColor(WHITE);
	} else {
		u8g2_display.setForegroundColor(WHITE);
		u8g2_display.setBackgroundColor(BLACK);
	}
}
void dispValBox(int v, int16_t n, bool inv){			// n is box 0-3
	invertColor(inv);
	u8g2centerNumber(v, n*32, hline*2+5, 32, 22);
}

void dispSymbBox(const char* v, int16_t n, bool inv){			// n is box 0-3
	invertColor(inv);
	u8g2centerText(v, n*32, hline*2+6, 32, 22);
}

void dispGenericMode(int submode, int selected){
	const char* legends[4] = {"","","",""};
	int legendVals[4] = {0,0,0,0};
	const char* legendText[4] = {"","","",""};
	switch(submode){
		case SUBMODE_MIDI:
			legends[0] = "OCT";
			legends[1] = "CH";
			legends[2] = "CC";
			legends[3] = "NOTE";
			legendVals[0] = (int)octave+4;
			legendVals[1] = midiChannel;
			legendVals[2] = potVal;
			legendVals[3] = midiLastNote;
			break;
		case SUBMODE_SEQ:
			legends[0] = "PTN";
			legends[1] = "TRSP";
			legends[2] = "SWNG"; //"TRSP";
			legends[3] = "BPM";
			legendVals[0] = playingPattern+1;
			legendVals[1] = (int)transpose;
			legendVals[2] = (int)patternSettings[playingPattern].swing; //(int)swing;
			// legendVals[2] =  swing_values[patternSettings[playingPattern].swing];
			legendVals[3] = (int)clockbpm;
			break;
		case SUBMODE_SEQ2:
			legends[0] = "SOLO";
			legends[1] = "LEN";
			legends[2] = "RATE";
			legends[3] = "CV"; //cvPattern
			legendVals[0] = patternSettings[playingPattern].solo; // playingPattern+1;
			legendVals[1] = PatternLength(playingPattern);
			legendVals[2] = -127;
			legendText[2] = mdivs[patternSettings[playingPattern].clockDivMultP];

			legendVals[3] = -127;
			if (cvPattern[playingPattern]) {
				legendText[3] = "On";
			} else {
				legendText[3] = "Off";
			}
			break;
		case SUBMODE_PATTPARAMS:
			legends[0] = "PTN";
			legends[1] = "LEN";
			legends[2] = "ROT";
			legends[3] = "CHAN";
			legendVals[0] = playingPattern+1;
			legendVals[1] = PatternLength(playingPattern);
			legendVals[2] = rotationAmt; //(int)transpose;
			legendVals[3] = PatternChannel(playingPattern);
			break;
		case SUBMODE_PATTPARAMS2:
			legends[0] = "START";
			legends[1] = "END";
			legends[2] = "FREQ";
			legends[3] = "PROB";
			legendVals[0] = patternSettings[playingPattern].startstep + 1;			// STRT step to autoreset on
			legendVals[1] = patternSettings[playingPattern].autoresetstep;			// STP step to autoreset on - 0 = no auto reset
			legendVals[2] = patternSettings[playingPattern].autoresetfreq; 			// FRQ to autoreset on -- every x cycles
			legendVals[3] = patternSettings[playingPattern].autoresetprob;			// PRO probability of resetting 0=NEVER 1=Always 2=50%
			break;
		case SUBMODE_PATTPARAMS3:
			legends[0] = "RATE";
			legends[1] = "SOLO";
			legends[2] = "---";
			legends[3] = "---";

			// RATE FOR CURR PATTERN
			legendVals[0] = -127;
			legendText[0] = mdivs[patternSettings[playingPattern].clockDivMultP];

			legendVals[1] = patternSettings[playingPattern].solo;
			legendVals[2] = 0; 			// TBD
			legendVals[3] = 0;			// TBD
			break;
		case SUBMODE_STEPREC:
			legends[0] = "OCT";
			legends[1] = "STEP";
			legends[2] = "NOTE";
			legends[3] = "PTN";
			legendVals[0] = (int)octave+4;
			legendVals[1] = seqPos[playingPattern]+1;
			legendVals[2] = stepNoteP[playingPattern][selectedStep].note; //(int)transpose;
			legendVals[3] = playingPattern+1;
			break;
		case SUBMODE_NOTESEL:
			legends[0] = "L-1";
			legends[1] = "L-2";
			legends[2] = "L-3";
			legends[3] = "L-4";
			for (int j=0; j<4; j++){

				int stepNoteParam = stepNoteP[playingPattern][selectedStep].params[j];
				if (stepNoteParam > -1){
					legendVals[j] = stepNoteParam;
				} else {
					legendVals[j] = -127;
					legendText[j] = "---";
				}
			}
			break;
		case SUBMODE_NOTESEL2:
			legends[0] = "NOTE";
			legends[1] = "OCT";
			legends[2] = "VEL";
			legends[3] = "LEN";
			legendVals[0] = stepNoteP[playingPattern][selectedStep].note;
			legendVals[1] = (int)octave+4;
			legendVals[2] = stepNoteP[playingPattern][selectedStep].vel;
			legendVals[3] = stepNoteP[playingPattern][selectedStep].len + 1;
			break;

		case SUBMODE_NOTESEL3:
			legends[0] = "TYPE";
			legends[1] = "PROB";
			legends[2] = "COND";
			legends[3] = "";
			legendVals[0] = -127;
			legendText[0] = stepTypes[stepNoteP[playingPattern][selectedStep].stepType];

			legendVals[1] = stepNoteP[playingPattern][selectedStep].prob;
//				String ac = String(trigConditionsAB[][0]);
//				String bc = String(trigConditionsAB[stepNoteP[playingPattern][selectedStep].condition][1]);

			legendVals[2] = -127;
			legendText[2] = trigConditions[stepNoteP[playingPattern][selectedStep].condition]; //ac + bc; // trigConditions

			legendVals[3] = 0;
			break;

		default:
			break;
	}


	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_LABELS);
	u8g2_display.setCursor(0, 0);
	dispGridBoxes();

	// labels
	u8g2_display.setForegroundColor(BLACK);
	u8g2_display.setBackgroundColor(WHITE);

	for (int j= 0; j<4; j++){
		u8g2centerText(legends[j], (j*32) + 1, hline-2, 32, 10);
	}

	// value text formatting
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_VALUES);
	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);


	switch(selected){
		case 0:
			display.fillRect(0*32+2, 11, 29, 21, WHITE);
			break;
		case 1: 	//
			display.fillRect(1*32+2, 11, 29, 21, WHITE);
			break;
		case 2: 	//
			display.fillRect(2*32+2, 11, 29, 21, WHITE);
			break;
		case 3: 	//
			display.fillRect(3*32+2, 11, 29, 21, WHITE);
			break;
		case 4: 	//
			break;
		default:
			break;
	}
	// ValueBoxes
	int highlight = false;
	for (int j = 0; j<4; j++){

		if (j == selected) {
			highlight = true;
		}else{
			highlight = false;
		}
		if (legendVals[j] == -127){
			dispSymbBox(legendText[j], j, highlight);
		} else {
			dispValBox(legendVals[j], j, highlight);
		}
	}
//	dispPageIndicators(1);

}

void dispPageIndicators(int page){
	display.fillRect(3*16, 30, 2, 2, WHITE);
}

void dispInfoDialog(){
	for (int key=0; key < NUM_DIALOGS; key++){
		if (infoDialog[key].state){
			dialogTimeout = 0;
			display.clearDisplay();
			u8g2_display.setFontMode(1);
			u8g2_display.setFont(FONT_TENFAT);
			u8g2_display.setForegroundColor(WHITE);
			u8g2_display.setBackgroundColor(BLACK);

			u8g2centerText(infoDialog[key].text, 0, 10, 128, 32);
			infoDialog[key].state = false;
		}
	}
}

void dispMode(){
	// labels formatting
	u8g2_display.setFontMode(1);
	u8g2_display.setFont(FONT_BIG);
	u8g2_display.setCursor(0, 0);

	u8g2_display.setForegroundColor(WHITE);
	u8g2_display.setBackgroundColor(BLACK);

	const char* displaymode = "";
	if (newmode != omxMode && enc_edit) {
		displaymode = modes[newmode]; // display.print(modes[newmode]);
	} else if (enc_edit) {
		displaymode = modes[omxMode]; // display.print(modes[mode]);
	}
	u8g2centerText(displaymode, 86, 20, 44, 32);
}


// ############## MAIN LOOP ##############

void loop() {
	customKeypad.tick();
	clksTimer = 0;

	Micros now = micros();
	Micros passed = now - lastProcessTime;
	lastProcessTime = now;

	if (passed > 0) {
		if (playing){
			advanceClock(passed);
			advanceSteps(passed);
		}
	}
	doStep();

	// DISPLAY SETUP
	display.clearDisplay();

	// ############### POTS ###############
	//
	readPotentimeters();


	// ############### ENCODER ###############
	//
	auto u = myEncoder.update();
	if (u.active()) {
			auto amt = u.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)
//    	Serial.println(u.dir() < 0 ? "ccw " : "cw ");
//    	Serial.println(amt);

		// Change Mode
			if (enc_edit) {
			// set mode
//			int modesize = NUM_OMX_MODES;
//			Serial.println(modesize);
				newmode = (OMXMode)constrain(newmode + amt, 0, NUM_OMX_MODES - 1);
				dispMode();
			dirtyDisplayTimer = displayRefreshRate+1;
				dirtyDisplay = true;

		} else if (!noteSelect && !patternParams && !stepRecord){
			switch(omxMode) {
				case MODE_OM: // Organelle Mother
					if (mimode == 4) {
						if(u.dir() < 0){									// if turn ccw
							MM::sendControlChange(CC_OM2,0,midiChannel);
						} else if (u.dir() > 0){							// if turn cw
							MM::sendControlChange(CC_OM2,127,midiChannel);
						}
					}
						dirtyDisplay = true;
//					break;
				case MODE_MIDI: // MIDI
					if (mimode == 1) { // set length
						int newchan = constrain(midiChannel + amt, 1, 16);
						if (newchan != midiChannel){
							midiChannel = newchan;
						}

					} else if (mimode == 0){
						// set octave
						newoctave = constrain(octave + amt, -5, 4);
						if (newoctave != octave){
							octave = newoctave;
						}
					}
						dirtyDisplay = true;
					break;
				case MODE_S1: // SEQ 1
					// FALL THROUGH
				case MODE_S2: // SEQ 2
					if (sqmode == 4 && sqmode2 == 4 ) {  // CHANGE PAGE
						sqpage = constrain(sqpage + amt, 0, 1);
					}

					// SEQ MODE PAGE 1
					if (sqmode == 0){
						playingPattern = constrain(playingPattern + amt, 0, 7);
						if (patternSettings[playingPattern].solo){
							setAllLEDS(0,0,0);
						}
					} else if (sqmode == 1){
						// set transpose
						transposeSeq(playingPattern, amt); //
						int newtransp = constrain(transpose + amt, -64, 63);
						transpose = newtransp;
					} else if (sqmode == 2){
						// set swing
						int newswing = constrain(patternSettings[playingPattern].swing + amt, 0, maxswing-1); // -1 to deal with display values
						swing = newswing;
						patternSettings[playingPattern].swing = newswing;
//						setGlobalSwing(newswing);
//						Serial.println(patternSettings[playingPattern].swing);
					} else if (sqmode == 3){
						// set tempo
						newtempo = constrain(clockbpm + amt, 40, 300);
						if (newtempo != clockbpm){
							// SET TEMPO HERE
							clockbpm = newtempo;
							resetClocks();
						}
					}

					// SEQ MODE PAGE 2
					if (sqmode2 == 0){
						// SET PLAYING PATTERN
//						playingPattern = constrain(playingPattern + amt, 0, 7);
						// MIDI SOLO
						patternSettings[playingPattern].solo = constrain(patternSettings[playingPattern].solo + amt, 0, 1);
						if (patternSettings[playingPattern].solo){
							setAllLEDS(0,0,0);
						}
					} else if (sqmode2 == 1){
						// SET PATTERN LENGTH
						SetPatternLength( playingPattern, constrain(PatternLength(playingPattern) + amt, 1, 16) );
					} else if (sqmode2 == 2){
						// SET CLOCK DIV/MULT
						patternSettings[playingPattern].clockDivMultP = constrain(patternSettings[playingPattern].clockDivMultP + amt, 0, NUM_MULTDIVS-1);
					} else if (sqmode2 == 3){
						// SET CV ON/OFF
						cvPattern[playingPattern] = constrain(cvPattern[playingPattern] + amt, 0, 1);
					}


						dirtyDisplay = true;
					break;
				default:
					break;
			}

		} else if (noteSelect || patternParams || stepRecord) {
			switch(omxMode) { // process encoder input depending on mode
				case MODE_MIDI: // MIDI
					break;
				case MODE_S1: // SEQ 1
						// FALL THROUGH

				case MODE_S2: // SEQ 2
					if (patternParams && !enc_edit){ 		// SEQUENCE PATTERN PARAMS MODE
						//
						if (ppmode == 4 && ppmode2 == 4 && ppmode3 == 4) {  // change page
							pppage = constrain(pppage + amt, 0, 2);		// HARDCODED - FIX WITH SIZE OF PAGES?
						}

						if (ppmode == 0) { 					// SET PLAYING PATTERN
							playingPattern = constrain(playingPattern + amt, 0, 7);
						}
						if (ppmode == 1) { 					// SET LENGTH
							SetPatternLength( playingPattern, constrain(PatternLength(playingPattern) + amt, 1, 16) );
						}
						if (ppmode == 2) { 					// SET PATTERN ROTATION
							int rotator;
							(u.dir() < 0 ? rotator = -1 : rotator = 1);
//							int rotator = constrain(rotcc, (PatternLength(playingPattern))*-1, PatternLength(playingPattern));
							rotationAmt = rotationAmt + rotator;
							if (rotationAmt < 16 && rotationAmt > -16 ){
								rotatePattern(playingPattern, rotator);
							}
							rotationAmt = constrain(rotationAmt, (PatternLength(playingPattern)-1)*-1, PatternLength(playingPattern)-1);
						}
						if (ppmode == 3) { 					// SET PATTERN CHANNEL
							patternSettings[playingPattern].channel = constrain(patternSettings[playingPattern].channel + amt, 0, 15);
						}

						if (ppmode3 == 0) { 					// SET CLOCK-DIV-MULT
							patternSettings[playingPattern].clockDivMultP = constrain(patternSettings[playingPattern].clockDivMultP + amt, 0, NUM_MULTDIVS-1); // set clock div/mult
						}
						if (ppmode3 == 1) { 					// SET MIDI SOLO
							patternSettings[playingPattern].solo = constrain(patternSettings[playingPattern].solo + amt, 0, 1);
						}

						// PATTERN PARAMS PAGE 2
							//TODO: convert to case statement ??
						if (ppmode2 == 0) { 					// SET AUTO START STEP
							patternSettings[playingPattern].startstep = constrain(patternSettings[playingPattern].startstep + amt, 0, patternSettings[playingPattern].len);
							//patternSettings[playingPattern].startstep--;
						}
						if (ppmode2 == 1) { 					// SET AUTO RESET STEP
							int tempresetstep = patternSettings[playingPattern].autoresetstep + amt;
							patternSettings[playingPattern].autoresetstep = constrain(tempresetstep, 0, patternSettings[playingPattern].len+1);
						}
						if (ppmode2 == 2) { 					// SET AUTO RESET FREQUENCY
							patternSettings[playingPattern].autoresetfreq = constrain(patternSettings[playingPattern].autoresetfreq + amt, 0, 15); // max every 16 times
						}
						if (ppmode2 == 3) { 					// SET AUTO RESET PROB
							patternSettings[playingPattern].autoresetprob = constrain(patternSettings[playingPattern].autoresetprob + amt, 0, 100); // never, 100% - 33%
						}

					} else if (stepRecord && !enc_edit){	// STEP RECORD MODE

						if (srmode == 4 && srmode2 == 4) { 	// CHANGE PAGE
							srpage = constrain(srpage + amt, 0, 1);		// HARDCODED - FIX WITH SIZE OF PAGES?
						}
						if (srmode == 3) {
//							playingPattern = constrain(playingPattern + amt, 0, 7);
						}
						if (srmode == 1) {
							if (u.dir() > 0){
								step_ahead(playingPattern);
							} else if (u.dir() < 0) {
								step_back(playingPattern);
							}
							selectedStep = seqPos[playingPattern];
						}
						if (srmode == 2) {
						}
						if (srmode == 0) {
							newoctave = constrain(octave + amt, -5, 4);
							if (newoctave != octave){
								octave = newoctave;
							}
						}
						if (srmode2 == 0) {
							changeStepType(amt);
						}
						if (srmode2 == 1) {
							int tempProb = stepNoteP[playingPattern][selectedStep].prob;
							stepNoteP[playingPattern][selectedStep].prob = constrain(tempProb + amt, 0, 100); // Note Len between 1-16
						}
						if (srmode2 == 2) {
							int tempCondition = stepNoteP[playingPattern][selectedStep].condition;
							stepNoteP[playingPattern][selectedStep].condition = constrain(tempCondition + amt, 0, 35); // 0-32
						}

					} else if (noteSelect && noteSelection && !enc_edit){	// NOTE SELECT MODE
						// {notenum,vel,len,p1,p2,p3,p4,p5}

						if (nsmode >= 0 && nsmode < 4){
//							Serial.print("nsmode ");
//							Serial.println(nsmode);
							if(u.dir() < 0){			// RESET PLOCK IF TURN CCW
								stepNoteP[playingPattern][selectedStep].params[nsmode] = -1;
							}
						}
						if (nsmode == 4 && nsmode2 == 4 && nsmode3 == 4) { 	// CHANGE PAGE
							nspage = constrain(nspage + amt, 0, 2);		// HARDCODED - FIX WITH SIZE OF PAGES?
//							Serial.print("nspage ");
//							Serial.println(nspage);
						}

						if (nsmode2 == 0) { 				// SET NOTE NUM
							int tempNote = stepNoteP[playingPattern][selectedStep].note;
							stepNoteP[playingPattern][selectedStep].note = constrain(tempNote + amt, 0, 127);
						}
						if (nsmode2 == 1) { 				// SET OCTAVE
							newoctave = constrain(octave + amt, -5, 4);
							if (newoctave != octave){
								octave = newoctave;
							}
						}
						if (nsmode2 == 2) { 				// SET VELOCITY
							int tempVel = stepNoteP[playingPattern][selectedStep].vel;
							stepNoteP[playingPattern][selectedStep].vel = constrain(tempVel + amt, 0, 127);
						}
						if (nsmode2 == 3) { 				// SET NOTE LENGTH
							int tempLen = stepNoteP[playingPattern][selectedStep].len;
							stepNoteP[playingPattern][selectedStep].len = constrain(tempLen + amt, 0, 15); // Note Len between 1-16
						}

						if (nsmode3 == 0) { 				// SET STEP TYPE
							changeStepType(amt);
						}
						if (nsmode3 == 1) { 				// SET STEP PROB
							int tempProb = stepNoteP[playingPattern][selectedStep].prob;
							stepNoteP[playingPattern][selectedStep].prob = constrain(tempProb + amt, 0, 100); // Note Len between 1-16
						}
						if (nsmode3 == 2) { 				// SET STEP TRIG CONDITION
							int tempCondition = stepNoteP[playingPattern][selectedStep].condition;
							stepNoteP[playingPattern][selectedStep].condition = constrain(tempCondition + amt, 0, 35); // 0-32
						}


					} else {
						newtempo = constrain(clockbpm + amt, 40, 300);
						if (newtempo != clockbpm){
							// SET TEMPO HERE
							clockbpm = newtempo;
							resetClocks();
						}
					}
					dirtyDisplay = true;
					break;

				case MODE_OM: // Organelle Mother
					break;

				default:
					break;
			}
		}
	}
	// END ENCODER

	// ############### ENCODER BUTTON ###############
	//
	auto s = encButton.update();
	switch (s) {
		// SHORT PRESS
		case Button::Down: //Serial.println("Button down");

			// what page are we on?
			if (newmode != omxMode && enc_edit) {
				omxMode = newmode;
				seqStop();
				setAllLEDS(0,0,0);
				enc_edit = false;
				dispMode();
			} else if (enc_edit){
				enc_edit = false;
			}

			if(omxMode == MODE_MIDI) {
				// switch midi oct/chan selection
				mimode = (mimode + 1 ) % 5;
//				mimode = !mimode;
			}
			if(omxMode == MODE_OM) {
				mimode = (mimode + 1 ) % 5;
//				MM::sendControlChange(CC_OM1,100,midiChannel);
			}
			if(omxMode == MODE_S1 || omxMode == MODE_S2) {
				if (noteSelect && noteSelection && !patternParams) {
					if (nspage == 0){
						nsmode2 = (nsmode2 + 1 ) % 5;
					}else if (nspage == 1){
						nsmode3 = (nsmode3 + 1 ) % 5;
					}else if (nspage == 2){
						nsmode = (nsmode + 1 ) % 5;
					}
				} else if (patternParams) {

					if (pppage == 0){
						// increment ppmode
						ppmode = (ppmode + 1 ) % 5;
					}else if (pppage == 1){
						ppmode2 = (ppmode2 + 1 ) % 5;
					}else if (pppage == 2){
						ppmode3 = (ppmode3 + 1) % 5;
					}
				} else if (stepRecord) {
					if (srpage == 0){
						srmode = (srmode + 1 ) % 5;
					} else if (srpage == 1){
						srmode2 = (srmode2 + 1 ) % 5;
					}

				} else {
					if (sqpage == 0){
						sqmode = (sqmode + 1 ) % 5;
					} else if (sqpage == 1){
						sqmode2 = (sqmode2 + 1 ) % 5;
					}
				}
			}
			dirtyDisplay = true;
			break;

		// LONG PRESS
		case Button::DownLong: //Serial.println("Button downlong");
			if (stepRecord) {
				resetPatternDefaults(playingPattern);
				clearedFlag = true;
			} else {
				enc_edit = true;
				newmode = omxMode;
				dispMode();
			}
			dirtyDisplay = true;

			break;
		case Button::Up: //Serial.println("Button up");
			if(omxMode == MODE_OM) {
//				MM::sendControlChange(CC_OM1,0,midiChannel);
			}
			break;
		case Button::UpLong: //Serial.println("Button uplong");
			break;
		default:
			break;
	}
	// END ENCODER BUTTON


	// ############### KEY HANDLING ###############

	while(customKeypad.available()){
		keypadEvent e = customKeypad.read();
		int thisKey = e.bit.KEY;
		int keyPos = thisKey - 11;

		if (e.bit.EVENT == KEY_JUST_PRESSED){
			keyState[thisKey] = true;
		}

		if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0 && enc_edit) {
			// temp - save whenever the 0 key is pressed in encoder edit mode
			saveToStorage();
			//	Serial.println("EEPROM saved");
		}

		switch(omxMode) {
			case MODE_OM: // Organelle
				// Fall Through

			case MODE_MIDI: // MIDI CONTROLLER

				// ### KEY PRESS EVENTS
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					//Serial.println(" pressed");
					midiNoteOn(thisKey, defaultVelocity, midiChannel);

				} else if(e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					//Serial.println(" released");
					midiNoteOff(thisKey, midiChannel);
				}

				// AUX KEY
				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {

					// Hard coded Organelle stuff
					MM::sendControlChange(CC_AUX, 100, midiChannel);
					if (midiAUX) {
						// STOP CLOCK
//						Serial.println("stop clock");

					} else {
						// START CLOCK
//						Serial.println("start clock");

					}
					midiAUX = !midiAUX;

				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {
					// Hard coded Organelle stuff
					MM::sendControlChange(CC_AUX, 0, midiChannel);
//					midiAUX = false;
				}
				break;

			case MODE_S1: // SEQUENCER 1
				// fall through

			case MODE_S2: // SEQUENCER 2
				// Sequencer row keys

				// ### KEY PRESS EVENTS

				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey != 0) {
					// set key timer to zero
					keyPressTime[thisKey] = 0;

					// NOTE SELECT
					if (noteSelect){
						if (noteSelection) {		// SET NOTE
							stepSelect = false;
							selectedNote = thisKey;
							int adjnote = notes[thisKey] + (octave * 12);
							stepNoteP[playingPattern][selectedStep].note = adjnote;
							if (!playing){
								seqNoteOn(thisKey, defaultVelocity, playingPattern);
							}
							// see RELEASE events for more
							dirtyDisplay = true;

						} else if (thisKey == 1) {

						} else if (thisKey == 2) {

						} else if (thisKey > 2 && thisKey < 11) { // Pattern select keys
							playingPattern = thisKey-3;
							dirtyDisplay = true;

						} else if ( thisKey > 10 ) {
							selectedStep = keyPos; // set noteSelection to this step
							stepSelect = true;
							noteSelection = true;
							dirtyDisplay = true;
						}

					// PATTERN PARAMS
					} else if (patternParams) {
						if (thisKey == 1) {


						} else if (thisKey == 2) {


						} else if (thisKey > 2 && thisKey < 11) { // Pattern select keys

							playingPattern = thisKey-3;

							// COPY / PASTE / CLEAR
							if (keyState[1] && !keyState[2]) {
								copyPattern(playingPattern);
								infoDialog[COPY].state = true; // copied flag
//								Serial.print("copy: ");
//								Serial.println(playingPattern);
							} else if (!keyState[1] && keyState[2]) {
								pastePattern(playingPattern);
								infoDialog[PASTE].state = true; // pasted flag
//								Serial.print("paste: ");
//								Serial.println(playingPattern);
							} else if (keyState[1] && keyState[2]) {
								clearPattern(playingPattern);
								infoDialog[CLEAR].state = true; // cleared flag
							}

							dirtyDisplay = true;
						} else if ( thisKey > 10 ) {
							// set pattern length with key
							SetPatternLength( playingPattern, thisKey - 10);
							dirtyDisplay = true;
						}

					// STEP RECORD
					} else if (stepRecord) {
						selectedNote = thisKey;
						selectedStep = seqPos[playingPattern];

						int adjnote = notes[thisKey] + (octave * 12);
						stepNoteP[playingPattern][selectedStep].note = adjnote;

						if (!playing){
							seqNoteOn(thisKey, defaultVelocity, playingPattern);
						} // see RELEASE events for more
						stepDirty = true;
						dirtyDisplay = true;

					// MIDI SOLO
					} else if (patternSettings[playingPattern].solo) {
						midiNoteOn(thisKey, defaultVelocity, patternSettings[playingPattern].channel+1);

					// REGULAR SEQ MODE
					} else {
						if (thisKey == 1) {
//							seqResetFlag = true;					// RESET ALL SEQUENCES TO FIRST/LAST STEP
																	// MOVED DOWN TO AUX KEY

						} else if (thisKey == 2) { 					// CHANGE PATTERN DIRECTION
//							patternSettings[playingPattern].reverse = !patternSettings[playingPattern].reverse;

						// BLACK KEYS
						} else if (thisKey > 2 && thisKey < 11) { // Pattern select

							// CHECK keyState[] FOR LONG PRESS THINGS

							// If KEY 1 is down + pattern and not playing = STEP RECORD
							if (keyState[1] && !playing) {
//								Serial.print("step record on - pattern: ");
//								Serial.println(thisKey-3);
								playingPattern = thisKey-3;
								seqPos[playingPattern] = 0;
								stepRecord = true;
								dirtyDisplay = true;

							// If KEY 2 is down + pattern = PATTERN MUTE
							} else if (keyState[2]) {
								patternSettings[thisKey-3].mute = !patternSettings[thisKey-3].mute;

							} else {
								playingPattern = thisKey-3;
								dirtyDisplay = true;
							}

						// SEQUENCE 1-16 STEP KEYS
						} else if (thisKey > 10) {
							// TOGGLE STEP ON/OFF
//							if ( stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_PLAY || stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_MUTE ) {
//								stepNoteP[playingPattern][keyPos].stepType = ( stepNoteP[playingPattern][keyPos].stepType == STEPTYPE_PLAY ) ? STEPTYPE_MUTE : STEPTYPE_PLAY;
//							}
							if ( stepNoteP[playingPattern][keyPos].trig == TRIGTYPE_PLAY || stepNoteP[playingPattern][keyPos].trig == TRIGTYPE_MUTE ) {
								stepNoteP[playingPattern][keyPos].trig = ( stepNoteP[playingPattern][keyPos].trig == TRIGTYPE_PLAY ) ? TRIGTYPE_MUTE : TRIGTYPE_PLAY;
							}
						}
					}
				}

				// ### KEY RELEASE EVENTS

				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0) {
					// MIDI SOLO
					if (patternSettings[playingPattern].solo) {
						midiNoteOff(thisKey, patternSettings[playingPattern].channel+1);
					}
				}

				if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey != 0 && (noteSelection || stepRecord) && selectedNote > 0) {
					if (!playing){
						seqNoteOff(thisKey, playingPattern);
					}
					if (stepRecord && stepDirty) {
						step_ahead(playingPattern);
						stepDirty = false;
					}
				}

				// AUX KEY PRESS EVENTS

				if (e.bit.EVENT == KEY_JUST_PRESSED && thisKey == 0) {

					if (noteSelect){
						if (noteSelection){
							selectedStep = 0;
							selectedNote = 0;
						} else {

						}
						noteSelection = false;
						noteSelect = !noteSelect;
						dirtyDisplay = true;

					} else if (patternParams){
						patternParams = !patternParams;
						dirtyDisplay = true;

					} else if (stepRecord){
						stepRecord = !stepRecord;
						dirtyDisplay = true;

					} else {
						if (keyState[1] || keyState[2]) { 				// CHECK keyState[] FOR LONG PRESS OF FUNC KEYS
							if (keyState[1]) {
								seqResetFlag = true;					// RESET ALL SEQUENCES TO FIRST/LAST STEP
								infoDialog[RESET].state = true; // reset flag

							} else if (keyState[2]) { 					// CHANGE PATTERN DIRECTION
								patternSettings[playingPattern].reverse = !patternSettings[playingPattern].reverse;
								if (patternSettings[playingPattern].reverse) {
									infoDialog[REV].state = true; // rev direction flag
								} else{
									infoDialog[FWD].state = true; // fwd direction flag
								}
							}
							dirtyDisplay = true;
						} else {
							if (playing){
								// stop transport
								playing = 0;
								allNotesOff();
	//							Serial.println("stop transport");
								seqStop();
							} else {
								// start transport
	//							Serial.println("start transport");
								seqStart();
							}
						}
					}

				// AUX KEY RELEASE EVENTS

				} else if (e.bit.EVENT == KEY_JUST_RELEASED && thisKey == 0) {

				}

//				strip.show();
				break;

			default:
				break;
		}
		// END MODE SWITCH

		if (e.bit.EVENT == KEY_JUST_RELEASED){
			keyState[thisKey] = false;
			keyPressTime[thisKey] = 0;
		}

	} // END KEYS WHILE


	// ### LONG KEY SWITCH PRESS
	for (int j=0; j<LED_COUNT; j++){
		if (keyState[j]){
			if (keyPressTime[j] >= longPressInterval && keyPressTime[j] < 9999){

				// DO LONG PRESS THINGS
				switch (omxMode){
					case MODE_MIDI:
						break;
					case MODE_S1:
						// fall through
					case MODE_S2:
						if (!patternSettings[playingPattern].solo){
							if (!keyState[1] && !keyState[2]) { // SKIP LONG PRESS IF FUNC KEYS ARE ALREDY HELD
								if (j > 2 && j < 11){ // skip AUX key, get pattern keys
									patternParams = true;
									dirtyDisplay = true;

								} else if (j > 10){
									if (!stepRecord && !patternParams){ 		// IGNORE LONG PRESSES IN STEP RECORD and Pattern Params
										selectedStep = j - 11; // set noteSelection to this step
										noteSelect = true;
										stepSelect = true;
										noteSelection = true;
										dirtyDisplay = true;
										// re-toggle the key you just held
//										if ( stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_PLAY || stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_MUTE ) {
//											stepNoteP[playingPattern][selectedStep].stepType = ( stepNoteP[playingPattern][selectedStep].stepType == STEPTYPE_PLAY ) ? STEPTYPE_MUTE : STEPTYPE_PLAY;
//										}
										if ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY || stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_MUTE ) {
											stepNoteP[playingPattern][selectedStep].trig = ( stepNoteP[playingPattern][selectedStep].trig == TRIGTYPE_PLAY ) ? TRIGTYPE_MUTE : TRIGTYPE_PLAY;
										}
									}
								}
							}
						}
						break;
					default:
						break;

				}
				keyPressTime[j] = 9999;
			}
		}
	}



	// ############### MODES DISPLAY  ##############

	switch(omxMode){
		case MODE_OM: 						// ############## ORGANELLE MODE
			// FALL THROUGH

		case MODE_MIDI:							// ############## MIDI KEYBOARD
			//playingPattern = 0; 		// DEFAULT MIDI MODE TO THE FIRST PATTERN SLOT
			midi_leds();				// SHOW LEDS

			if (dirtyDisplay){			// DISPLAY
				if (!enc_edit){
					dispGenericMode(SUBMODE_MIDI, mimode);
				}
			}
			break;

		case MODE_S1: 						// ############## SEQUENCER 1
			// FALL THROUGH
		case MODE_S2: 						// ############## SEQUENCER 2
			if (!enc_edit) {
				if (dialogTimeout > dialogDuration && dialogTimeout < dialogDuration + 20) {
					dirtyDisplay = true;
				}
			}
			// MIDI SOLO
			if (patternSettings[playingPattern].solo) {
				midi_leds();
			}

			if (dirtyDisplay){			// DISPLAY
				if (!enc_edit){
					if (!noteSelect and !patternParams and !stepRecord){
						if (sqpage == 0){
							dispGenericMode(SUBMODE_SEQ, sqmode);
						} else if (sqpage == 1){
							dispGenericMode(SUBMODE_SEQ2, sqmode2);
						}
						dispInfoDialog();
					}
					if (noteSelect) {
						if (nspage == 0){
							dispGenericMode(SUBMODE_NOTESEL2, nsmode2);
						} else if (nspage == 1){
							dispGenericMode(SUBMODE_NOTESEL3, nsmode3);
						} else if (nspage == 2){
							dispGenericMode(SUBMODE_NOTESEL, nsmode);
						}
					}
					if (patternParams) {
						if (pppage == 0){
							dispGenericMode(SUBMODE_PATTPARAMS, ppmode);
//							dispPatternParams();
						} else if (pppage == 1){
							dispGenericMode(SUBMODE_PATTPARAMS2, ppmode2);
//							dispPatternParams2();
						} else if (pppage == 2){
							dispGenericMode(SUBMODE_PATTPARAMS3, ppmode3);
							// dispPatternParams3();
						}
						dispInfoDialog();

					}
					if (stepRecord) {
						if (srpage == 0){
							dispGenericMode(SUBMODE_STEPREC, srmode);
							dispInfoDialog();
						} else if (srpage == 1){
							dispGenericMode(SUBMODE_NOTESEL3, srmode2);
							dispInfoDialog();
						}
					}
				}
			}

			break;

		default:
			break;

	}


	// DISPLAY at end of loop

	if (dirtyDisplay){
		if (dirtyDisplayTimer > displayRefreshRate) {
			display.display();
			dirtyDisplay = false;
			dirtyDisplayTimer = 0;
		}
	}


	// are pixels dirty
	if (dirtyPixels){
		strip.show();
		dirtyPixels = false;
	}

	while (MM::usbMidiRead()) {
		// ignore incoming messages
	}
	while (MM::midiRead()) {
		// ignore incoming messages
	}

} // ######## END MAIN LOOP ########


// ####### SEQENCER FUNCTIONS

void step_ahead(int patternNum) {
	// step each pattern ahead one place
	for (int j=0; j<8; j++){
		if (patternSettings[j].reverse) {
			seqPos[j]--;
			auto_reset(j); // determine whether to reset or not based on param settings
//			if (seqPos[j] < 0)
//				seqPos[j] = PatternLength(j)-1;
		} else {
			seqPos[j]++;
 			auto_reset(j); // determine whether to reset or not based on param settings
//			if (seqPos[j] >= PatternLength(j))
//				seqPos[j] = 0;
		}
	}
}
void step_back(int patternNum) {
	// step each pattern ahead one place
	for (int j=0; j<8; j++){
		if (patternSettings[j].reverse) {
			seqPos[j]++;
			auto_reset(j); // determine whether to reset or not based on param settings
		} else {
			seqPos[j]--;
// 			auto_reset(j);
			if (seqPos[j] < 0)
				seqPos[j] = PatternLength(j)-1;
		}
	}
}

void new_step_ahead(int patternNum) {
	// step each pattern ahead one place
		if (patternSettings[patternNum].reverse) {
			seqPos[patternNum]--;
			auto_reset(patternNum); // determine whether to reset or not based on param settings
		} else {
			seqPos[patternNum]++;
			auto_reset(patternNum); // determine whether to reset or not based on param settings
		}
}

void auto_reset(int p){
	// should be conditioned on whether we're in S2!!
	if ( seqPos[p] >= PatternLength(p) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep > (patternSettings[p].startstep) ) && (seqPos[p] >= patternSettings[p].autoresetstep)) ||
		 (patternSettings[p].autoreset && (patternSettings[p].autoresetstep == 0 ) && (seqPos[p] >= patternSettings[p].rndstep)) ||
		 (patternSettings[p].reverse && (seqPos[p] < 0)) || // normal reverse reset
		 (patternSettings[p].reverse && patternSettings[p].autoreset && (seqPos[p] < patternSettings[p].startstep )) // ||
		 //(patternSettings[p].reverse && patternSettings[p].autoreset && (patternSettings[p].autoresetstep == 0 ) && (seqPos[p] < patternSettings[p].rndstep))
		 ) {

		if (patternSettings[p].reverse) {
			if (patternSettings[p].autoreset){
				if (patternSettings[p].autoresetstep == 0){
					seqPos[p] = patternSettings[p].rndstep-1;
				}else{
					seqPos[p] = patternSettings[p].autoresetstep-1; // resets pattern in REV
				}
			} else {
				seqPos[p] = (PatternLength(p)-patternSettings[p].startstep)-1;
			}

		} else {
			seqPos[p] = (patternSettings[p].startstep); // resets pattern in FWD
		}
		if (patternSettings[p].autoresetfreq == patternSettings[p].current_cycle){ // reset cycle logic
			if (probResult(patternSettings[p].autoresetprob)){
				// chance of doing autoreset
				patternSettings[p].autoreset = true;
			} else {
				patternSettings[p].autoreset = false;
			}
			patternSettings[p].current_cycle = 1; // reset cycle to start new iteration
		} else {
			patternSettings[p].autoreset = false;
			patternSettings[p].current_cycle++; // advance to next cycle
		}
		patternSettings[p].rndstep = (rand() % PatternLength(p)) + 1; // randomly choose step for next cycle
	}
// return ()
}

bool probResult(int probSetting){
//	int tempProb = (rand() % probSetting);
//	Serial.println(tempProb);
 	if (probSetting == 0){
 		return false;
 	}
	if((rand() % 100) < probSetting){ // assumes probSetting is a range 0-100
 		return true;
 	} else {
 		return false;
 	}
 }

bool evaluate_AB(int condition, int patternNum) {
	bool shouldTrigger = false;;

	loopCount[patternNum][seqPos[patternNum]]++;

	int a = trigConditionsAB[condition][0];
	int b = trigConditionsAB[condition][1];

//Serial.print (patternNum);
//Serial.print ("/");
//Serial.print (seqPos[patternNum]);
//Serial.print (" ");
//Serial.print (loopCount[patternNum][seqPos[patternNum]]);
//Serial.print (" ");
//Serial.print (a);
//Serial.print (":");
//Serial.print (b);
//Serial.print (" ");

	if (loopCount[patternNum][seqPos[patternNum]] == a){
		shouldTrigger = true;
	} else {
		shouldTrigger = false;
	}
	if (loopCount[patternNum][seqPos[patternNum]] >= b){
		loopCount[patternNum][seqPos[patternNum]] = 0;
	}
//	Serial.println (shouldTrigger);
	return shouldTrigger;
}

void changeStepType(int amount){
	auto tempType = stepNoteP[playingPattern][selectedStep].stepType + amount;

	// this is fucking hacky to increment the enum for stepType
	switch(tempType){
		case 0:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_NONE;
			break;
		case 1:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RESTART;
			break;
		case 2:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_FWD;
			break;
		case 3:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_REV;
			break;
		case 4:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_PONG;
			break;
		case 5:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RANDSTEP;
			break;
		case 6:
			stepNoteP[playingPattern][selectedStep].stepType = STEPTYPE_RAND;
			break;
		default:
			break;
	}
	//							Serial.println(stepNoteP[playingPattern][selectedStep].stepType);
}
void step_on(int patternNum){
//		Serial.print(patternNum);
//		Serial.println(" step on");
//	playNote(playingPattern);
}

void step_off(int patternNum, int position){
	//	Serial.print(seqPos[patternNum]);
	//	Serial.println(" step off");
	lastNote[patternNum][position] = 0;

//      analogWrite(CVPITCH_PIN, 0);
//      digitalWrite(CVGATE_PIN, LOW);
}

void doStep() {
// // probability test
	bool testProb = probResult(stepNoteP[playingPattern][seqPos[playingPattern]].prob);


	switch(omxMode){
		case MODE_S1:
			if(playing) {
				// ############## STEP TIMING ##############
//				if(micros() >= nextStepTime){
				if(micros() >= timePerPattern[playingPattern].nextStepTimeP){
					seqReset();
					// DO STUFF

//					int lastPos = (seqPos[playingPattern]+15) % 16;
//					if (lastNote[playingPattern][lastPos] > 0){
//						step_off(playingPattern, lastPos);
//					}
//					lastStepTime = nextStepTime;
//					nextStepTime += step_micros;

					timePerPattern[playingPattern].lastPosP = (seqPos[playingPattern]+15) % 16;
					if (lastNote[playingPattern][timePerPattern[playingPattern].lastPosP] > 0){
						step_off(playingPattern, timePerPattern[playingPattern].lastPosP);
					}
					timePerPattern[playingPattern].lastStepTimeP = timePerPattern[playingPattern].nextStepTimeP;
					timePerPattern[playingPattern].nextStepTimeP += (step_micros)*( multValues[patternSettings[playingPattern].clockDivMultP] ); // calc step based on rate

					if (testProb){ //  && evaluate_AB(stepNoteP[playingPattern][seqPos[playingPattern]].condition, playingPattern)
						playNote(playingPattern);
	//					step_on(playingPattern);
					}


					show_current_step(playingPattern); // show led for step
					step_ahead(playingPattern);
				}
			} else {
				show_current_step(playingPattern);
			}
			break;

		case MODE_S2:
			if(playing) {
				unsigned long playstepmicros = micros();

				for (int j=0; j<NUM_PATTERNS; j++){ // check all patterns for notes to play in time

					// CLOCK PER PATTERN BASED APPROACH
						if(playstepmicros >= timePerPattern[j].nextStepTimeP){

						seqReset(); // check for seqReset
						timePerPattern[j].lastStepTimeP = timePerPattern[j].nextStepTimeP;
						timePerPattern[j].nextStepTimeP += (step_micros)*( multValues[patternSettings[j].clockDivMultP] ); // calc step based on rate

						// only play if not muted
						if (!patternSettings[j].mute) {
							timePerPattern[j].lastPosP = (seqPos[j]+15) % 16;
							if (lastNote[j][timePerPattern[j].lastPosP] > 0){
								step_off(j, timePerPattern[j].lastPosP);
							}
							if (testProb){
								if (evaluate_AB(stepNoteP[j][seqPos[j]].condition, j)){
									playNote(j);
								}
							}
						}
//						show_current_step(playingPattern);
						if(j == playingPattern){ // only show selected pattern
														show_current_step(playingPattern);
												}
						new_step_ahead(j);
					}
				}


			} else {
				show_current_step(playingPattern);
			}
			break;

		default:
			break;
	}
}

void cvNoteOn(int notenum){
	if (notenum>=midiLowestNote && notenum <midiHightestNote){
		pitchCV = static_cast<int>(roundf( (notenum - midiLowestNote) * stepsPerSemitone)); // map (adjnote, 36, 91, 0, 4080);
		digitalWrite(CVGATE_PIN, HIGH);
		analogWrite(CVPITCH_PIN, pitchCV);
	}
}
void cvNoteOff(){
	digitalWrite(CVGATE_PIN, LOW);
//	analogWrite(CVPITCH_PIN, 0);
}

// #### MIDI Mode note on/off
void midiNoteOn(int notenum, int velocity, int channel) {
	int adjnote = notes[notenum] + (octave * 12); // adjust key for octave range
	if (adjnote>=0 && adjnote <128){
		midiLastNote = adjnote;

		// keep track of adjusted note when pressed so that when key is released we send
		// the correct note off message
		midiKeyState[notenum] = adjnote;

		MM::sendNoteOn(adjnote, velocity, channel);
		// CV
		cvNoteOn(adjnote);
	}

	strip.setPixelColor(notenum, MIDINOTEON);         //  Set pixel's color (in RAM)
	dirtyPixels = true;
	dirtyDisplay = true;
}

void midiNoteOff(int notenum, int channel) {
	// we use the key state captured at the time we pressed the key to send the correct note off message
	int adjnote = midiKeyState[notenum];
	if (adjnote>=0 && adjnote <128){
		MM::sendNoteOff(adjnote, 0, channel);
		// CV off
		cvNoteOff();
	}

	strip.setPixelColor(notenum, LEDOFF);
	dirtyPixels = true;
	dirtyDisplay = true;
}

// #### SEQ Mode note on/off
void seqNoteOn(int notenum, int velocity, int patternNum){
	int adjnote = notes[notenum] + (octave * 12); // adjust key for octave range
	if (adjnote>=0 && adjnote <128){
		lastNote[patternNum][seqPos[patternNum]] = adjnote;
		MM::sendNoteOn(adjnote, velocity, PatternChannel(playingPattern));

		// keep track of adjusted note when pressed so that when key is released we send
		// the correct note off message
		midiKeyState[notenum] = adjnote;

		// CV
		if (cvPattern[playingPattern]){
			cvNoteOn(adjnote);
		}
	}

	strip.setPixelColor(notenum, MIDINOTEON);         //  Set pixel's color (in RAM)
	dirtyPixels = true;
	dirtyDisplay = true;
}

void seqNoteOff(int notenum, int patternNum){
	// we use the key state captured at the time we pressed the key to send the correct note off message
	int adjnote = midiKeyState[notenum];
	if (adjnote>=0 && adjnote <128){
		MM::sendNoteOff(adjnote, 0, PatternChannel(playingPattern));
		// CV off
		if (cvPattern[playingPattern]){
			cvNoteOff();
		}
	}

	strip.setPixelColor(notenum, LEDOFF);
	dirtyPixels = true;
	dirtyDisplay = true;
}


// Play a note / step (SEQUENCERS)
void playNote(int patternNum) {
//	Serial.println(stepNoteP[patternNum][seqPos[patternNum]].note); // Debug
	bool sendnoteCV = false;
	int rnd_swing;
	if (cvPattern[patternNum]){
		sendnoteCV = true;
	}
	StepType playStepType = stepNoteP[patternNum][seqPos[patternNum]].stepType;

	if (stepNoteP[patternNum][seqPos[patternNum]].stepType == STEPTYPE_RAND){
		auto tempType = random(STEPTYPE_COUNT);

		// this is fucking hacky to increment the enum for stepType
		switch(tempType){
			case 0:
				playStepType = STEPTYPE_NONE;
				break;
			case 1:
				playStepType = STEPTYPE_RESTART;
				break;
			case 2:
				playStepType = STEPTYPE_FWD;
				break;
			case 3:
				playStepType = STEPTYPE_REV;
				break;
			case 4:
				playStepType = STEPTYPE_PONG;
				break;
			case 5:
				playStepType = STEPTYPE_RANDSTEP;
				break;
		}
//		Serial.println(playStepType);
	}

	switch (playStepType) {
		case STEPTYPE_COUNT:	// fall through
		case STEPTYPE_RAND:
			break;
		case STEPTYPE_NONE:
			break;
		case STEPTYPE_FWD:
			patternSettings[patternNum].reverse = 0;
			break;
		case STEPTYPE_REV:
			patternSettings[patternNum].reverse = 1;
			break;
		case STEPTYPE_PONG:
			patternSettings[patternNum].reverse = !patternSettings[patternNum].reverse;
			break;
		case STEPTYPE_RANDSTEP:
			seqPos[patternNum] = (rand() % PatternLength(patternNum)) + 1;
			break;
		case STEPTYPE_RESTART:
			seqPos[patternNum] = 0;
			break;
		break;
	}

	// regular note on trigger

	if (stepNoteP[patternNum][seqPos[patternNum]].trig == TRIGTYPE_PLAY){

		seq_velocity = stepNoteP[patternNum][seqPos[patternNum]].vel;

		noteoff_micros = micros() + ( stepNoteP[patternNum][seqPos[patternNum]].len + 1 )* step_micros ;
		pendingNoteOffs.insert(stepNoteP[patternNum][seqPos[patternNum]].note, PatternChannel(patternNum), noteoff_micros, sendnoteCV );

		if (seqPos[patternNum] % 2 == 0){

			if (patternSettings[patternNum].swing < 99){
				noteon_micros = micros() + ((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * patternSettings[patternNum].swing); // full range swing
//				Serial.println((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * patternSettings[patternNum].swing);
//			} else if ((patternSettings[patternNum].swing > 50) && (patternSettings[patternNum].swing < 99)){
//			   noteon_micros = micros() + ((step_micros * multValues[patternSettings[patternNum].clockDivMultP]) * ((patternSettings[patternNum].swing - 50)* .01) ); // late swing
//			   Serial.println(((step_micros * multValues[patternSettings[patternNum].clockDivMultP]) * ((patternSettings[patternNum].swing - 50)* .01) ));
			} else if (patternSettings[patternNum].swing == 99){ // random drunken swing
				rnd_swing = rand() % 95 + 1; // rand 1 - 95 // randomly apply swing value
				noteon_micros = micros() + ((ppqInterval * multValues[patternSettings[patternNum].clockDivMultP])/(PPQ / 24) * rnd_swing);
			}

		} else {
			noteon_micros = micros();
		}

		// Queue note-on
		pendingNoteOns.insert(stepNoteP[patternNum][seqPos[patternNum]].note, seq_velocity, PatternChannel(patternNum), noteon_micros, sendnoteCV );

		// {notenum, vel, notelen, step_type, {p1,p2,p3,p4}, prob}
		// send param locks
		for (int q=0; q<4; q++){
			int tempCC = stepNoteP[patternNum][seqPos[patternNum]].params[q];
			if (tempCC > -1) {
				MM::sendControlChange(pots[q],tempCC,PatternChannel(patternNum));
				prevPlock[q] = tempCC;
			} else if (prevPlock[q] != potValues[q]) {
				//if (tempCC != prevPlock[q]) {
				MM::sendControlChange(pots[q],potValues[q],PatternChannel(patternNum));
				prevPlock[q] = potValues[q];
			}
		}
		lastNote[patternNum][seqPos[patternNum]] = stepNoteP[patternNum][seqPos[patternNum]].note;

		// CV is sent from pendingNoteOns/pendingNoteOffs

	}
}

void allNotesOff() {
	pendingNoteOffs.allOff();
}

void allNotesOffPanic() {
	analogWrite(CVPITCH_PIN, 0);
	digitalWrite(CVGATE_PIN, LOW);
	for (int j=0; j<128; j++){
		MM::sendNoteOff(j, 0, midiChannel);  // NEEDS FIXING
	}
}

void transposeSeq(int patternNum, int amt) {
	for (int k=0; k<NUM_STEPS; k++){
		stepNoteP[patternNum][k].note += amt;
	}
}

void seqReset(){
	if (seqResetFlag) {
		for (int k=0; k<NUM_PATTERNS; k++){
			for (int q=0; q<NUM_STEPS; q++){
				loopCount[k][q] = 0;
			}
			if (patternSettings[k].reverse) { // REVERSE
				seqPos[k] = PatternLength(k) - 1;
			} else {
				seqPos[k] = 0;
			}
		}
		MM::stopClock();
		MM::startClock();
		seqResetFlag = false;
	}
}

void seqStart() {
	playing = 1;


	for (int x=0; x<NUM_PATTERNS; x++){
		timePerPattern[x].nextStepTimeP = micros();
		timePerPattern[x].lastStepTimeP = micros();
	}

	if (!seqResetFlag) {
		MM::continueClock();
//	} else if (seqPos[playingPattern]==0) {
//		MM::startClock();
	}
}

void seqStop() {
	ticks = 0;
	playing = 0;
	MM::stopClock();
	allNotesOff();
}

void seqContinue() {
	playing = 1;
}

void rotatePattern(int patternNum, int rot) {
	if ( patternNum < 0 || patternNum >= NUM_PATTERNS )
		return;
	int size = PatternLength(patternNum);
	StepNote arr[size];
	rot = (rot + size) % size;
	for (int d = rot, s = 0; s < size; d = (d+1) % size, ++s)
		arr[d] = stepNoteP[patternNum][s];
	for (int i = 0; i < size; ++i)
		stepNoteP[patternNum][i] = arr[i];
}

void resetPatternDefaults(int patternNum){
	for (int i = 0; i < NUM_STEPS; i++){
		// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
		stepNoteP[patternNum][i].note = patternDefaultNoteMap[patternNum];
		stepNoteP[patternNum][i].len = 0;
	}
}

void clearPattern(int patternNum){
	for (int i = 0; i < NUM_STEPS; i++){
		// {notenum,vel,len,stepType,{p1,p2,p3,p4,p5}}
		stepNoteP[patternNum][i].note = patternDefaultNoteMap[patternNum];
		stepNoteP[patternNum][i].vel = defaultVelocity;
		stepNoteP[patternNum][i].len = 0;
		stepNoteP[patternNum][i].stepType = STEPTYPE_NONE;
		stepNoteP[patternNum][i].params[0] = -1;
		stepNoteP[patternNum][i].params[1] = -1;
		stepNoteP[patternNum][i].params[2] = -1;
		stepNoteP[patternNum][i].params[3] = -1;
		stepNoteP[patternNum][i].params[4] = -1;
		stepNoteP[patternNum][i].prob = 100;
		stepNoteP[patternNum][i].condition = 0;
	}
}

void copyPattern(int patternNum){
	//for( int i = 0 ; i < NUM_STEPS ; ++i ){
	//	copyPatternBuffer[i] = stepNoteP[patternNum][i];
	//}

	memcpy( &copyPatternBuffer, &stepNoteP[patternNum], NUM_STEPS * sizeof(StepNote) );
}

void pastePattern(int patternNum){
	//for( int i = 0 ; i < NUM_STEPS ; ++i ){
	//	stepNoteP[patternNum][i] = copyPatternBuffer[i] ;
	//}

	memcpy( &stepNoteP[patternNum], &copyPatternBuffer, NUM_STEPS * sizeof(StepNote) );
}

void u8g2centerText(const char* s, int16_t x, int16_t y, uint16_t w, uint16_t h) {
//  int16_t bx, by;
	uint16_t bw, bh;
	bw = u8g2_display.getUTF8Width(s);
	bh = u8g2_display.getFontAscent();
	u8g2_display.setCursor(
		x + (w - bw) / 2,
		y + (h - bh) / 2
	);
	u8g2_display.print(s);
}

void u8g2centerNumber(int n, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	char buf[8];
	itoa(n, buf, 10);
	u8g2centerText(buf, x, y, w, h);
}


// #### LED STUFF
// Rainbow cycle along whole strip. Pass delay time (in ms) between frames.
void rainbow(int wait) {
	// Hue of first pixel runs 5 complete loops through the color wheel.
	// Color wheel has a range of 65536 but it's OK if we roll over, so
	// just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
	// means we'll make 5*65536/256 = 1280 passes through this outer loop:
	for(long firstPixelHue = 0; firstPixelHue < 1*65536; firstPixelHue += 256) {
		for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
			// Offset pixel hue by an amount to make one full revolution of the
			// color wheel (range of 65536) along the length of the strip
			// (strip.numPixels() steps):
			int pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());

			// strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
			// optionally add saturation and value (brightness) (each 0 to 255).
			// Here we're using just the single-argument hue variant. The result
			// is passed through strip.gamma32() to provide 'truer' colors
			// before assigning to each pixel:
			strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
		}
		strip.show(); // Update strip with new contents
		delay(wait);  // Pause for a moment
	}
}
void setAllLEDS(int R, int G, int B) {
	for(int i=0; i<LED_COUNT; i++) { // For each pixel...
		strip.setPixelColor(i, strip.Color(R, G, B));
	}
	dirtyPixels = true;
}

// #### OLED STUFF
void testdrawrect(void) {
	display.clearDisplay();

	for(int16_t i=0; i<display.height()/2; i+=2) {
		display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SSD1306_WHITE);
		display.display(); // Update screen with each newly-drawn rectangle
		delay(1);
	}

	delay(500);
}

void drawLoading(void) {
	const char* loader[] = {"\u25f0", "\u25f1", "\u25f2", "\u25f3"};
	display.clearDisplay();
	u8g2_display.setFontMode(0);
	for(int16_t i=0; i<16; i+=1) {
		display.clearDisplay();
		u8g2_display.setCursor(18,18);
		u8g2_display.setFont(FONT_TENFAT);
		u8g2_display.print("OMX-27");
		u8g2_display.setFont(FONT_SYMB_BIG);
		u8g2centerText(loader[i%4], 80, 10, 32, 32); // "\u00BB\u00AB" // // dice: "\u2685"
		display.display();
		delay(100);
	}

	delay(100);
}

void initPatterns( void ) {
	// default to GM Drum Map for now -- GET THIS FROM patternDefaultNoteMap instead
//	uint8_t initNotes[NUM_PATTERNS] = {
//		36,
//		38,
//		37,
//		39,
//		42,
//		46,
//		49,
//		51 };

	StepNote stepNote = { 0, 100, 0, TRIGTYPE_MUTE, { -1, -1, -1, -1, -1 }, 100, 0, STEPTYPE_NONE };
					// {note, vel, len, TRIGTYPE, {params0, params1, params2, params3, params4}, prob, condition, STEPTYPE}

	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		stepNote.note = patternDefaultNoteMap[i];		// Defined in sequencer.h
		for ( int j=0; j<NUM_STEPS; j++ ) {
			memcpy( &stepNoteP[i][j], &stepNote, sizeof(StepNote) );
		}

		patternSettings[i].len = 15;
		patternSettings[i].channel = i;		// 0 - 15 becomes 1 - 16
		patternSettings[i].mute = false;
		patternSettings[i].reverse = false;
		patternSettings[i].swing = 0;
		patternSettings[i].startstep = 0;
		patternSettings[i].autoresetstep = 0;
		patternSettings[i].autoresetfreq = 0;
		patternSettings[i].autoresetprob = 0;
		patternSettings[i].current_cycle = 1;
		patternSettings[i].rndstep = 3;
		patternSettings[i].autoreset = false;
		patternSettings[i].solo = false;
	}
}

void saveHeader( void ) {
	// 1 byte for EEPROM version
	storage->write( EEPROM_HEADER_ADDRESS + 0, EEPROM_VERSION );

	// 1 byte for mode
	storage->write( EEPROM_HEADER_ADDRESS + 1, (uint8_t)omxMode );

	// 1 byte for the active pattern
	storage->write( EEPROM_HEADER_ADDRESS + 2, (uint8_t)playingPattern );

	// 1 byte for Midi channel
	uint8_t unMidiChannel = (uint8_t)( midiChannel - 1 );
	storage->write( EEPROM_HEADER_ADDRESS + 3, unMidiChannel );

	for ( int i=0; i<NUM_CC_POTS; i++ ) {
		storage->write( EEPROM_HEADER_ADDRESS + 4 + i, pots[i] );
	}

	// 23 bytes remain for header fields
}

// returns true if the header contained initialized data
// false means we shouldn't attempt to load any further information
bool loadHeader( void ) {
	uint8_t version = storage->read(EEPROM_HEADER_ADDRESS + 0);

	//char buf[64];
	//snprintf( buf, sizeof(buf), "EEPROM Header Version is %d\n", version );
	//Serial.print( buf );

	// Uninitalized EEPROM memory is filled with 0xFF
	if ( version == 0xFF ) {
		// EEPROM was uninitialized
		//Serial.println( "version was 0xFF" );
		return false;
	}

	if ( version != EEPROM_VERSION ) {
		// write an adapter if we ever need to increment the EEPROM version and also save the existing patterns
		// for now, return false will essentially reset the state
		return false;
	}

	omxMode = (OMXMode)storage->read( EEPROM_HEADER_ADDRESS + 1 );

	playingPattern = storage->read( EEPROM_HEADER_ADDRESS + 2 );

	uint8_t unMidiChannel = storage->read( EEPROM_HEADER_ADDRESS + 3 );
	midiChannel = unMidiChannel + 1;

	for ( int i=0; i<NUM_CC_POTS; i++ ) {
		pots[i] = storage->read( EEPROM_HEADER_ADDRESS + 4 + i );
	}

	return true;
}

void savePatterns( void ) {
	int nLocalAddress = EEPROM_PATTERN_ADDRESS;
	int s = sizeof( StepNote );

	// storage->writeObject uses storage->write under the hood, so writes here of the same data are a noop

	// for each pattern
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		for ( int j=0; j<NUM_STEPS; j++ ) {
			storage->writeObject( nLocalAddress, stepNoteP[i][j] );
			nLocalAddress += s;
		}
	}

	nLocalAddress = EEPROM_PATTERN_SETTINGS_ADDRESS;
	s = sizeof( PatternSettings );

	// save pattern settings
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		storage->writeObject( nLocalAddress, patternSettings[i] );
		nLocalAddress += s;
	}
}

void loadPatterns( void ) {
	//Serial.println( "load patterns" );

	int nLocalAddress = EEPROM_PATTERN_ADDRESS;
	int s = sizeof( StepNote );

	// for each pattern
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		for ( int j=0; j<NUM_STEPS; j++ ) {
			storage->readObject( nLocalAddress, stepNoteP[i][j] );
			nLocalAddress += s;
		}
	}

	nLocalAddress = EEPROM_PATTERN_SETTINGS_ADDRESS;
	s = sizeof( PatternSettings );

	// load pattern length
	for ( int i=0; i<NUM_PATTERNS; i++ ) {
		storage->readObject( nLocalAddress, patternSettings[i] );
		nLocalAddress += s;
	}
}

// currently saves everything ( mode + patterns )
void saveToStorage( void ) {
	//Serial.println( "saving..." );
	saveHeader();
	savePatterns();
}

// currently loads everything ( mode + patterns )
bool loadFromStorage( void ) {
	// This load can happen soon after Serial.begin - enable this 'wait for Serial' if you need to Serial.print during loading
	//while( !Serial );

	bool bContainedData = loadHeader();

	//Serial.println( "read the header" );

	if ( bContainedData ) {
		//Serial.println( "loading patterns" );
		loadPatterns();
		return true;
	}

	return false;
}
