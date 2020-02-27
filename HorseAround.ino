#include <LiquidCrystal.h>
#include <avr/pgmspace.h>
#include <limits.h>
#include <EEPROM.h>

enum Selected {
  SELECTED_NONE,
  SELECTED_PHASE,
  SELECTED_MINUTE,
  SELECTED_SECOND,
  SELECTED_COUNT,
  SELECTED_SPEED,
};

const int rs = 8, en = 9, d4 = 4, d5 = 5, d6 = 6, d7 = 7;
const int backlightPin = 10; // active high, I think
const int forwardPin = 13;
const int backwardPin = 12;
const int speedPin = 11; // (PWM)
const int emergencyStopButton = 3;
const int jogSpeed = 51;

// RIGHT UP DOWN LEFT SELECT START
const int buttonCount = 6;
const int digitalButtons[buttonCount] = {A1, A2, A3, A4, A5, 2};
const int debounceNeeded = 3;

const int keyRepeatPhaseCount = 4;
const int keyRepeatPhases[keyRepeatPhaseCount] = {1000, 500, 500, 100};

const int restTimeAddress = 0;

struct State{
  void (*enterFunction)(void);
  struct Action {
    void (*leaveFunction)(void);
    const State *nextState;
    const bool repeat;
  } edges[buttonCount];
};

const int maxPhaseCount = 10;

struct Phase {
  unsigned char time; // unit: 10 s
  unsigned char count;
  unsigned char speed;
};

struct Program{
  int repeats;
  Phase phases[maxPhaseCount];
};

Program program;
const Phase defaultEmptyPhase = {30, 0, 50};
const Phase defaultFirstPhase = {30, 6, 50};
Phase phaseBackup;

int phase = 0;

int lastButton = buttonCount;
int nextButton = buttonCount;
int debounceCount = 0;
char lcdBuf[64];
int restTime = 10;
volatile bool stopFlag = false;
bool keyRepeatEligible = false;
int keyRepeatPhase = 0;
int keyRepeatCount = 0;
int currentProgram = 0;
const int programCount = (EEPROM.length() - sizeof(restTime)) / sizeof(Program);

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// ö: \xef
// ü: \xf5

int readButton(void) {
  for(int i = 0; i < buttonCount; i++){
    if(digitalRead(digitalButtons[i]) == LOW) return i;
  }
  return -1;
}

int getProgramAddress(int programID){
  return sizeof(restTime) + programID * sizeof(Program);
}

void loadCurrentProgram(void){
  EEPROM.get(getProgramAddress(currentProgram), program);
}

void saveCurrentProgram(void){
  EEPROM.put(getProgramAddress(currentProgram), program);
}

void saveRestTime(void){
  EEPROM.put(restTimeAddress, restTime);
}

bool doPartUntil(const char *upperText, const char *lowerText, long nextMillis) {
  long lastSeconds = -1;
  long remain;
  long firstMillis = millis();
  while((remain = nextMillis - millis()) > 0){
    long seconds = (remain + 999) / 1000;
    if(seconds != lastSeconds){
      lcd.clear();
      sprintf(lcdBuf, upperText, (int)(seconds / 60), (int)(seconds % 60));
      lcd.print(lcdBuf);
      sprintf(lcdBuf, lowerText, (int)(seconds / 60), (int)(seconds % 60));
      lcd.setCursor(0, 1);
      lcd.print(lcdBuf);
      lastSeconds = seconds;
    }
    if(readButton() == 4) return true;
    if(!stopFlag) return true;
  }
  return false;
}

bool verifyStopButton(){
  if(digitalRead(emergencyStopButton) == LOW){
    stopFlag = true;
    return true;
  } else {
    lcd.clear();
    lcd.print("Veszleallito hiba");
    delay(1000);
    return false;
  }  
}

void runHorses(void) {
  int phaseCount;
  bool reverse = false;
  long nextMillis = millis();
  char upperBuf[64], lowerBufRun[64];
  const char restPattern[] = "Pihen\xef... %d:%02d";

  if(!verifyStopButton()) return;

  for(phaseCount = 0; phaseCount < maxPhaseCount; phaseCount++) {
    if(program.phases[phaseCount].count == 0) break;
  }
  for(int repeat = 0; repeat < program.repeats; repeat++) {
    for(int phase = 0; phase < phaseCount; phase++){
      sprintf(lowerBufRun, "%d%%%% %%d:%%02d", program.phases[phase].speed);
      analogWrite(speedPin, ((int)program.phases[phase].speed) * 255 / 100);
      for(int part = 0; part < program.phases[phase].count; part++){
        sprintf(upperBuf, "%d/%d, %d/%d, %d/%d", repeat + 1, program.repeats, phase + 1, phaseCount, part + 1, program.phases[phase].count);
        nextMillis += (long)restTime * 1000;
        if(doPartUntil(upperBuf, restPattern, nextMillis)) return;
        if(reverse) {
          digitalWrite(backwardPin, HIGH);
        } else {
          digitalWrite(forwardPin, HIGH);
        }
        nextMillis += (long)program.phases[phase].time * 10000;
        bool stop = doPartUntil(upperBuf, lowerBufRun, nextMillis);
        if(reverse) {
          digitalWrite(backwardPin, LOW);
        } else {
          digitalWrite(forwardPin, LOW);
        }
        if(stop) return;
        reverse = !reverse;
      }
    }
  }
}

void showRun(void){
  long totalTime = 0;
  for(int phase = 0; phase < maxPhaseCount; phase++){
    if(program.phases[phase].count == 0) break;
    totalTime += (((long) program.phases[phase].time) * 10 + restTime) * program.phases[phase].count;
  }
  Serial.println(totalTime);
  totalTime *= program.repeats;
  sprintf(lcdBuf, "P[%d]: %ld:%02ld", currentProgram + 1, totalTime / 60, totalTime % 60);
  lcd.print("Futtat!");
  lcd.setCursor(0, 1);
  lcd.print(lcdBuf);
}

void showSetup(void){
  int phaseCount;
  for(phaseCount = 0; phaseCount < maxPhaseCount; phaseCount++) {
    if(program.phases[phaseCount].count == 0) break;
  }
  sprintf(lcdBuf, "P[%d]: %hdx%d gy", currentProgram + 1, program.repeats, phaseCount);
  lcd.print("Beallitas");
  lcd.setCursor(0, 1);
  lcd.print(lcdBuf);
}

void showRestSetup(void){
  sprintf(lcdBuf, "Pihen\xef: [%d] mp", restTime);
  lcd.print(lcdBuf);
}

void restIncrease(void){
  restTime++;
}

void restDecrease(void){
  if(restTime > 0)
  restTime--;
}

void showPhaseGeneric(enum Selected selected){
  char phaseBuf[8], minuteBuf[8], secondBuf[8], countBuf[8], speedBuf[8];
  sprintf(phaseBuf, selected == SELECTED_PHASE ? "[%d]" : "%d", phase + 1);
  sprintf(minuteBuf, selected == SELECTED_MINUTE ? "[%d]" : "%d", program.phases[phase].time / 6);
  sprintf(secondBuf, selected == SELECTED_SECOND ? "[%02d]" : "%02d", program.phases[phase].time % 6 * 10);
  sprintf(countBuf, selected == SELECTED_COUNT ? "[%d]" : "%d", program.phases[phase].count);
  sprintf(speedBuf, selected == SELECTED_SPEED ? "[%d]" : "%d", program.phases[phase].speed);
  sprintf(lcdBuf, "%s: %s:%s x %s", phaseBuf, minuteBuf, secondBuf, countBuf);
  lcd.print(lcdBuf);
  lcd.setCursor(0, 1);
  sprintf(lcdBuf, "%s%%", speedBuf);
  lcd.print(lcdBuf);
}

void showPhaseOverview(void){
  showPhaseGeneric(SELECTED_PHASE);
}

void phaseIncrease(void){
  if(program.phases[phase].count == 0 || phase == maxPhaseCount){
    phase = 0;
  } else {
    phase++;
  }
  phaseBackup = program.phases[phase];
}

void phaseDecrease(void){
  if(phase > 0) {
    phase--;
  } else {
    phase = 0;
    while(program.phases[phase].count > 0 && phase + 1 < maxPhaseCount) phase++;
  }
  phaseBackup = program.phases[phase];
}

void selectFirstPhase(void){
  phase = 0;
  phaseBackup = program.phases[phase];
}

void showRepeatsSetup(void){
  lcd.print("Egeszet ismetel:");
  lcd.setCursor(0, 1);
  sprintf(lcdBuf, "[%d] x", program.repeats);
  lcd.print(lcdBuf);
}


void repeatsIncrease(void){
  if(program.repeats < INT_MAX);
  program.repeats++;
}

void repeatsDecrease(void){
  if(program.repeats >= 0)
  program.repeats--;
}

void showOk(void){
  lcd.print("Ok");
}

void showErase(void){
  lcd.print("T\xefr\xefl?");
}

void eraseProgram(void){
  program.repeats = 1;
  program.phases[0] = defaultFirstPhase;
  for(int i = 1; i < maxPhaseCount; i++) {
    program.phases[i] = defaultEmptyPhase;
  }
  selectFirstPhase();
}

void showPhaseLengthMinutesSetup(void) {
  showPhaseGeneric(SELECTED_MINUTE);
}

void phaseLengthIncreaseMinutes(void) {
  if(program.phases[phase].time <= UCHAR_MAX - 6);
  program.phases[phase].time += 6;
}

void phaseLengthDecreaseMinutes(void) {
  if(program.phases[phase].time >= 6)
  program.phases[phase].time -= 6;
}

void showPhaseLengthSecondsSetup(void) {
  showPhaseGeneric(SELECTED_SECOND);
}

void phaseLengthIncreaseSeconds(void) {
  if(program.phases[phase].time < UCHAR_MAX);
  program.phases[phase].time++;
}

void phaseLengthDecreaseSeconds(void) {
  if(program.phases[phase].time > 0)
  program.phases[phase].time--;
}

void showPhaseCountSetup(void) {
  showPhaseGeneric(SELECTED_COUNT);
}

void phaseCountIncrease(void) {
  if(program.phases[phase].count < UCHAR_MAX)
  program.phases[phase].count++;
}

void phaseCountDecrease(void){
  if(program.phases[phase].count > 0)
  program.phases[phase].count--;
}

void showPhaseSpeedSetup(void) {
  showPhaseGeneric(SELECTED_SPEED);

}

void phaseSpeedIncrease(void) {
  if(program.phases[phase].speed < 100)
  program.phases[phase].speed++;
}

void phaseSpeedDecrease(void) {
  if(program.phases[phase].speed > 0)
  program.phases[phase].speed--;
}

void phaseOk(void) {
  if(program.phases[phase].count == 0){
    if(phase + 2 < maxPhaseCount) {
      if(program.phases[phase + 1].count == 0){
        program.phases[phase + 1] = defaultEmptyPhase;
      } else {
        for(int i = phase; i + 2 < maxPhaseCount; i++) {
          program.phases[i] = program.phases[i + 1];
        }
        program.phases[maxPhaseCount - 1] = defaultEmptyPhase;
      }
    }
  }
}

int asciiTablePage = 0;

void showASCIITable(void) {
  for(int i = 0; i < 16; i++)
    lcd.write(asciiTablePage * 16 + i);
  lcd.setCursor(0, 1);
  lcd.print(asciiTablePage);
}

void increaseASCII(void){
  asciiTablePage++;
}

void decreaseASCII(void){
  asciiTablePage--;
}

void showJog(void){
  lcd.print("Leptet");
  analogWrite(speedPin, jogSpeed);
}

void jog(bool forward){
  if(!verifyStopButton()) return;
  if(forward){
    digitalWrite(forwardPin, HIGH);
  } else {
    digitalWrite(backwardPin, HIGH);
  }
  while(stopFlag && (readButton() == nextButton)) delay(1);
  digitalWrite(forwardPin, LOW);
  digitalWrite(backwardPin, LOW);
}

void jogForward(void){
  jog(true);
}

void jogReverse(void){
  jog(false);
}

void showFormat(void){
  lcd.print("Mindet t\xefr\xefl?");
}

void showFormatConfirm(void){
  lcd.print("Biztos mindet t\xefr\xefl?");
  lcd.setCursor(0, 1);
  lcd.print("Igen: fel");
}

void format(void){
  lcd.clear();
  lcd.print("Varjon");
  restTime = 10;
  saveRestTime();
  program.repeats = 1;
  program.phases[0] = defaultFirstPhase;
  for(int i = 1; i < maxPhaseCount; i++) {
    program.phases[i] = defaultEmptyPhase;
  }
  for(currentProgram = 0; currentProgram < programCount; currentProgram++){
    saveCurrentProgram();
  }
  currentProgram = 0;
  lcd.clear();
  lcd.print("Kesz");
  delay(1000);
}

void nextProgram(void){
  if(currentProgram < programCount - 1) {
    currentProgram++;
  } else {
    currentProgram = 0;
  }
  loadCurrentProgram();
}

void prevProgram(void){
  if(currentProgram > 0) {
    currentProgram--;
  } else {
    currentProgram = programCount - 1;
  }
  loadCurrentProgram();
}

void restorePhase(void){
  program.phases[phase] = phaseBackup;
}

// right, up, down, left, select, run
extern const PROGMEM State runState;
extern const PROGMEM State setupState;
extern const PROGMEM State restSetupState;
extern const PROGMEM State jogState;
extern const PROGMEM State formatState;
extern const PROGMEM State formatConfirmState;
extern const PROGMEM State setupPhaseOverviewState;
extern const PROGMEM State setupPhaseLengthMinutesState;
extern const PROGMEM State setupPhaseLengthSecondsState;
extern const PROGMEM State setupPhaseCountState;
extern const PROGMEM State setupPhaseSpeedState;
extern const PROGMEM State setupRepeatsState;
extern const PROGMEM State setupOkState;
extern const PROGMEM State setupEraseState;
extern const PROGMEM State asciiTableState;
//                                                                                  RIGHT                                   UP                                                                  DOWN                                                                LEFT                                    SELECT                                        START
const PROGMEM State runState =                      {showRun,                       {{NULL, &setupState},                   {nextProgram, &runState},                                           {prevProgram, &runState},                                           {NULL, &formatState},                   {NULL, NULL},                                 {runHorses, &runState}}};
const PROGMEM State setupState =                    {showSetup,                     {{NULL, &restSetupState},               {nextProgram, &setupState},                                         {prevProgram, &setupState},                                         {NULL, &runState},                      {NULL, NULL},                                 {selectFirstPhase, &setupPhaseOverviewState}}};
const PROGMEM State restSetupState =                {showRestSetup,                 {{saveRestTime, &jogState},             {restIncrease, &restSetupState, true},                              {restDecrease, &restSetupState, true},                              {saveRestTime, &setupState},            {NULL, NULL},                                 {NULL, NULL}}};
const PROGMEM State jogState =                      {showJog,                       {{NULL, &formatState},                  {jogForward, &jogState},                                            {jogReverse, &jogState},                                            {NULL, &restSetupState},                {NULL, NULL},                                 {NULL, NULL}}};
const PROGMEM State formatState =                   {showFormat,                    {{NULL, &runState},                     {NULL, NULL},                                                       {NULL, NULL},                                                       {NULL, &jogState},                      {NULL, NULL},                                 {NULL, &formatConfirmState}}};
const PROGMEM State formatConfirmState =            {showFormatConfirm,             {{NULL, &formatState},                  {format, &runState},                                                {NULL, &formatState},                                               {NULL, &formatState},                   {NULL, &formatState},                         {NULL, &formatState}}};
const PROGMEM State setupPhaseOverviewState =       {showPhaseOverview,             {{NULL, &setupPhaseLengthMinutesState}, {phaseIncrease, &setupPhaseOverviewState},                          {phaseDecrease, &setupPhaseOverviewState},                          {NULL, &setupEraseState},               {loadCurrentProgram, &setupState},            {saveCurrentProgram, &setupState}}};
const PROGMEM State setupPhaseLengthMinutesState =  {showPhaseLengthMinutesSetup,   {{NULL, &setupPhaseLengthSecondsState}, {phaseLengthIncreaseMinutes, &setupPhaseLengthMinutesState, true},  {phaseLengthDecreaseMinutes, &setupPhaseLengthMinutesState, true},  {phaseOk, &setupPhaseOverviewState},    {restorePhase, &setupPhaseOverviewState},     {phaseOk, &setupPhaseOverviewState}}};
const PROGMEM State setupPhaseLengthSecondsState =  {showPhaseLengthSecondsSetup,   {{NULL, &setupPhaseCountState},         {phaseLengthIncreaseSeconds, &setupPhaseLengthSecondsState, true},  {phaseLengthDecreaseSeconds, &setupPhaseLengthSecondsState, true},  {NULL, &setupPhaseLengthMinutesState},  {restorePhase, &setupPhaseOverviewState},     {phaseOk, &setupPhaseOverviewState}}};
const PROGMEM State setupPhaseCountState =          {showPhaseCountSetup,           {{NULL, &setupPhaseSpeedState},         {phaseCountIncrease, &setupPhaseCountState, true},                  {phaseCountDecrease, &setupPhaseCountState, true},                  {NULL, &setupPhaseLengthSecondsState},  {restorePhase, &setupPhaseOverviewState},     {phaseOk, &setupPhaseOverviewState}}};
const PROGMEM State setupPhaseSpeedState =          {showPhaseSpeedSetup,           {{phaseOk, &setupRepeatsState},         {phaseSpeedIncrease, &setupPhaseSpeedState, true},                  {phaseSpeedDecrease, &setupPhaseSpeedState, true},                  {NULL, &setupPhaseCountState},          {restorePhase, &setupPhaseOverviewState},     {phaseOk, &setupPhaseOverviewState}}};
const PROGMEM State setupRepeatsState =             {showRepeatsSetup,              {{NULL, &setupOkState},                 {repeatsIncrease, &setupRepeatsState, true},                        {repeatsDecrease, &setupRepeatsState, true},                        {NULL, &setupPhaseSpeedState},          {loadCurrentProgram, &setupState},            {NULL, NULL}}};
const PROGMEM State setupOkState =                  {showOk,                        {{NULL, &setupEraseState},              {NULL, NULL},                                                       {NULL, NULL},                                                       {NULL, &setupRepeatsState},             {loadCurrentProgram, &setupState},            {saveCurrentProgram, &setupState}}};
const PROGMEM State setupEraseState =               {showErase,                     {{NULL, &setupPhaseOverviewState},      {NULL, NULL},                                                       {NULL, NULL},                                                       {NULL, &setupOkState},                  {loadCurrentProgram, &setupState},            {eraseProgram, &setupPhaseOverviewState}}};
const PROGMEM State asciiTableState =               {showASCIITable,                {{NULL, NULL},                          {increaseASCII, &asciiTableState},                                  {decreaseASCII, &asciiTableState},                                  {NULL, NULL},                           {NULL, NULL},                                 {NULL, NULL}}};


const State *state = &runState;

void emergencyStop(void) {
  digitalWrite(forwardPin, LOW);
  digitalWrite(backwardPin, LOW);
  stopFlag = false;
}

void setup(void) {
  Serial.begin(9600);
  // Read stuff from EEPROM
  EEPROM.get(restTimeAddress, restTime);
  loadCurrentProgram();
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  for(int i = 0; i < buttonCount; i++){
    pinMode(digitalButtons[i], INPUT_PULLUP);
  }
  pinMode(forwardPin, OUTPUT);
  pinMode(backwardPin, OUTPUT);
  pinMode(speedPin, OUTPUT);
  pinMode(emergencyStopButton, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(emergencyStopButton), emergencyStop, RISING);
  pinMode(backlightPin, OUTPUT);
  digitalWrite(backlightPin, HIGH);

  void (*enterFunction)(void);
  enterFunction = (void (*)(void))pgm_read_ptr(&(state->enterFunction));
  enterFunction();
}

void transitionState(int nextButton) {
  void (*edgeFunction)(void);
  edgeFunction = (void (*)(void))pgm_read_ptr(&(state->edges[nextButton].leaveFunction));
  if(edgeFunction != NULL) edgeFunction();
  const State *nextState = (const State *)pgm_read_ptr(&(state->edges[nextButton].nextState));
  if(nextState != NULL) {
    state = nextState;
    lcd.clear();
    void (*enterFunction)(void);
    enterFunction = (void (*)(void))pgm_read_ptr(&(state->enterFunction));
    if(enterFunction  != NULL) enterFunction();
  }
}

void loop(void) {
  int pressedButton = readButton();

  if(pressedButton == nextButton) {
    debounceCount++;
  } else {
    debounceCount = 1;
    nextButton = pressedButton;
    keyRepeatEligible = false;
  }
  if(debounceCount >= debounceNeeded && lastButton != nextButton) {
    lastButton = nextButton;

    if(nextButton >= 0){
      keyRepeatEligible = (bool)pgm_read_byte(&(state->edges[nextButton].repeat));
      transitionState(nextButton);
      keyRepeatPhase = 0;
      keyRepeatCount = 0;
    }
  }
  if(keyRepeatEligible){
    keyRepeatCount++;
    if(keyRepeatCount >= keyRepeatPhases[keyRepeatPhase]){
      keyRepeatCount = 0;
      if(keyRepeatPhase + 1 < keyRepeatPhaseCount) keyRepeatPhase++;
      transitionState(nextButton);
    }
  }

  delay(1);
}
