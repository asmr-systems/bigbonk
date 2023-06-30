#include "note_map.h"

/** :::: configuration. */
const  unsigned int NumberOfPads               = 9;
uint8_t*            midi_channel               = DefaultMidiChannelMap;
uint8_t*            midi_note                  = DefaultMidiNoteMap;
const  unsigned int GateDuration               = 100;                      // length (ms) of gate signal.

/** :::: parameters.    */
const  unsigned int ADCMax                  = 1024;
const  unsigned int hysteresis              = (float)ADCMax*0.05;

/** :::: state.         */
static unsigned int peak[NumberOfPads]      = { 0     };    // peak within a trigger event.
static bool         triggered[NumberOfPads] = { false };    // triggered state of a pad.
static bool         onset[NumberOfPads]     = { false };    // gate onset of a pad.
static unsigned int velocity[NumberOfPads]  = { 0     };    // velocity of trigger event.
static unsigned int gate_for[NumberOfPads]  = { 0     };    // remaining time (ms) to gate.


#define MIDI_FREQ_HZ      31250
// #define MIDI_FREQ_HZ      38400  // DEBUGGING
const uint8_t MIDI_CMD_NOTE_ON = 0x90;
#define MIDI_CMD_NOTE_OFF 0x80

#define ADC_0_PIN A7
#define ADC_1_PIN A6
#define INPUT_SELECT_0 A2
#define INPUT_SELECT_1 A1
#define INPUT_SELECT_2 A0


void set_input_select(uint8_t n) {
    if (n < 8) {
        digitalWrite(INPUT_SELECT_0, 0x01&n);
        digitalWrite(INPUT_SELECT_1, 0x01&(n>>1));
        digitalWrite(INPUT_SELECT_2, 0x01&(n>>2));
    }
}

void setup() {
    Serial.begin(MIDI_FREQ_HZ);

    pinMode(INPUT_SELECT_0, OUTPUT);
    pinMode(INPUT_SELECT_1, OUTPUT);
    pinMode(INPUT_SELECT_2, OUTPUT);

    midi_note = SP404_Bank_1;
}

void loop() {
    scan_inputs();
    set_gates();

    // TODO: check for range changes.
}

unsigned int read_pad_voltage(uint8_t n) {
    // since the HC4051 only has 8 mux channels and we have
    // 9 pads, we need to use an additional ADC pin on the MCU.
    if (n < NumberOfPads-1) {
        set_input_select(n);
        return analogRead(ADC_0_PIN);
    }
    if (n == NumberOfPads-1) return analogRead(ADC_1_PIN);

    // we should not be getting here.
    return 0;
}

void scan_inputs() {
    for (uint8_t i = 0; i < NumberOfPads; i++) {

        unsigned int level = read_pad_voltage(i);

        if (!triggered[i]) {
            if (level > (peak[i] + hysteresis)) {
                // a trigger event has begun.
                triggered[i] = true;
                peak[i]      = level;
            }
            else if (level <= (peak[i] - hysteresis)) {
                // lower the peak level.
                peak[i] = level;
            }
        }
        else {
            if (level > peak[i]) {
                // track the increasing peak.
                peak[i] = level;
            }
            else if (level <= (peak[i] - hysteresis)) {
                // triggered event is complete.
                velocity[i]  = (uint8_t)(((float)peak[i]/(float)ADCMax) * 127.0);
                gate_for[i]  = GateDuration;
                triggered[i] = false;
                peak[i]      = level;
                onset[i]     = true;
            }
        }
    };
}

void set_gates() {
    static unsigned long prev_time  = micros();
    unsigned long        curr_time  = micros();
    unsigned int         delta_ms   = (float)(curr_time - prev_time)/1000;

    for (uint8_t i = 0; i < NumberOfPads; i++) {
        // ignore pads that are not currently being gated.
        if (gate_for[i] == 0) continue;

        if (onset[i]) {
            // we just started gating.
            gate_for[i] = GateDuration;

            set_midi_note_on(i);
            // TODO set cv gate on

            onset[i] = false;
        }
        else {
            if (gate_for[i] <= delta_ms) {
                // we are finished gating.

                set_midi_note_off(i);
                // TODO set cv gate off

                gate_for[i] = 0;
            }
            else {
                // we are still gating.

                // TODO adjust midi velocity?
                // TODO adjust cv velocity?

                gate_for[i] -= delta_ms;
            }
        }
    }

    prev_time = curr_time;
}


void set_midi_note_on(uint8_t n) {
    Serial.write(MIDI_CMD_NOTE_ON|midi_channel[n]);  // transmit note on command.
    Serial.write(midi_note[n]);                      // send the note data.
    Serial.write(velocity[n]);
}

void set_midi_note_off(uint8_t n) {
    Serial.write(MIDI_CMD_NOTE_OFF|midi_channel[n]);  // transmit note off command.
    Serial.write(midi_note[n]);                       // send the note data.
    Serial.write(0);
}
