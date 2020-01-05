#include <ros.h>
#include <math.h>
#include <string.h>
//!====================Pin Map====================
//? RC
#define FWDPULSEPIN 2   //RC Channel 2
#define ANGPULSEPIN 3   //RC Channel 1
#define ESTOPPULSEPIN 4 //RC Channel 3
//? Switch Toggle
#define SWITCHAPIN 7
#define SWITCHBPIN 8
#define SWITCHCPIN 9
#define SWITCHDPIN 10
//? RGB Addressable LED
#define LED_R 21
#define LED_G 22
#define LED_B 23
//? Estop Signal From Estop Board
#define ESTOPPIN 11 //Singal from board
#define SOFTWAREENABLEPIN 16 //Signal to board
//? Sabertooth Motor Controller
#define SABERTOOTHPINRX 0
#define SABERTOOTHPINTX 1
//? Encoder
#define LEFTENCODER_A 17
#define LEFTENCODER_B 18
#define RIGHTENCODER_A 19
#define RIGHTENCODER_B 20


//!====================Serial Code Name====================
#define terminalSerial Serial
#define sabertoothSerial Serial1


//!====================Switches====================
//positions for physical switch data in the Switches array
#define SWITCH_A 0      //autonomous enable
#define SWITCH_B 1      //currently unused
#define SWITCH_C 2      //currently unused
#define SWITCH_D 3      //currently unused
#define SWITCH_ESTOP 4  //Estop Control board treated as switch
//for storing the states of the physical switches
volatile boolean Switches[6];


//!====================Motor Controller====================
//max output to Sabertooth
#define SABERTOOTHMAX 127
//for communicating with the Sabertooth controller
int SabertoothAddress = B10000010;        // set Address to 130
int SabertoothMask = B01111111;
int Checksum0 = B00000000;                                         // set
int LeftMotorDirection = B00000001;                                     // set Motor 1 backwards
int LeftMotorSpeed = B00000000;                                         // set Motor 1 speed to 0 to start
int RightMotorDirection = B00000100;                                     // set Motor 2 forwards
int RightMotorSpeed = B00000000;                                         // set Motor 2 speed to 0 to start
int Checksum1 = (SabertoothAddress + LeftMotorDirection + LeftMotorSpeed); // Check other Motor 1 commands against this
int Checksum2 = (SabertoothAddress + RightMotorDirection + RightMotorSpeed); // Check other Motor 2 commands against this

// Motor Command:
int leftMotorDirection = B00000000; 
int rightMotorDirection = B00000000;

//!====================State Machine====================
#define STATE_ESTOP 0
#define STATE_RC 1
#define STATE_AUTONOMOUS 2
// Global variable for storing the robot state. All writable
int State; 
int NextState;


//!====================Timing====================
//for timing of debug output
long LastDebug = 0;
//for timing of the main ControlLoop
long ControlLoopLastTime = 0;
//for timing of the Sabertooth controller output loop
long MotorCommandLastSent = 0;
//For Controlling last time published to ROS
long ROSLastSent = 0;
//milliseconds between outputting debug info (1 hz)
#define DEBUGOUTPUTRATE 100
//milliseconds between running the control loop (50 hz)
#define CONTROLLOOPRATE 20
//milliseconds between running the motor controller output loop (50 hz=20ms)
#define SABERTOOTHLOOPRATE 20
// Teensy as snesor interface, publish rate control (60HZ)
#define ROSPUBLISHRATE 16.6667

//!====================RC ONLY====================
//total number of RC channels
#define RCNUMBEROFCHANNELS 3
//positions for pin PWM data in the RCPulseData array
#define FWDPULSEDATA 0
#define ANGPULSEDATA 1
#define ESTOPPULSEDATA 2
//holds the temporary start of signal data, for use in timing the PWM width of the RC input signals
volatile uint32_t RCStartPulse[RCNUMBEROFCHANNELS];
//holds data from the RC PWM inputs
volatile uint32_t RCTempPulseData[RCNUMBEROFCHANNELS];
//more permenant pulse data copied over from RCTempPulseData
uint32_t RCPulseData[RCNUMBEROFCHANNELS];
//constants relating to the RC functioning
int RCCenter = 1500;
int RCMax = 2000;
int RCMin = 1000;
int EStopThreshold = 1500;


//!====================Inverse Kinematics====================
//kinematics calculations from old code
//uint32_t MaxRCDesiredTicksPerSecond = 51330; //(2 m/s) * (1 rev/0.957557m) * (24 motor rev/wheel rev) * (1024 ticks/motor rev)
//float TicksToMotorSpeedMultiplier = 0.00247419;// Max motor command of 127 divided by max desired ticks per second of 51330
//physical constants for calculating speeds
float WheelSpacing = 0.77; //Wheel spacing (center to center) in meters; assumed from previous code, double check
float MaxMotorRPM = 1;  //need this
float WheelDiameter = 0.957557;   //assumed from previous code, double check
float MaxDesiredSpeed = 2;  //max speed of 2 m/s


//!====================Encoder====================
//encoder cycles per revolution of the wheel
int ENCODERTICKPERREV=1024;
int MotorRevsPerWheelRev=24;  //assumed from previous code, double check

//constants for referencing values in the encoder structures
#define LEFTENCODER 0
#define RIGHTENCODER 1

volatile long leftEncoderPos = 0;
volatile long rightEncoderPos = 0;

volatile boolean leftAset = false;
volatile boolean leftBset = false;
volatile boolean rightAset = false;
volatile boolean rightBset = false;


long lastLeftPos = 0;
long lastRightPos = 0;
long lastEncoderTime = 0;



//!====================PID motor level====================
long PIDLastTime = 0;
//PID constants, move to start of code once PID verified working
float kP_left = 0.22;
float kI_left = 0.05;
float kD_left = 0.001;
float kP_right = 0.22;
float kI_right = 0.05;
float kD_right = 0.001;

// Global Variables
float left_speed_error_old = 0.0;
float right_speed_error_old = 0.0;
float left_speed_error = 0.0;
float right_speed_error = 0.0;
float left_speed_error_sum = 0.0;
float right_speed_error_sum = 0.0;

//PID erro sum cap
float pidErrorCap = 4000.0;
//!====================Global Velocity Control====================
//? What is human readable
float rcLinearSpeed;
float rcAngularSpeed;
float autoLinearVelocity;
float autoAngularVelocity;
//? What is in each motor perspective
float desLeftMotorSpeed;
float desRightMotorSpeed;
//? What is PID say?
float pidedLeftMotorSpeed;
float pidedRightMotorSpeed;
float cmdLeftMotorSpeed;
float cmdRightMotorSpeed;
//? Encoder reading?
float obsLinearVelocity;
float obsAngularVelocity;
float obsLeftMotorSpeed;
float obsRightMotorSpeed;

//!====================Kinematics Calibration Profile====================
float l_motorConstant = 1.0;
float r_motorConstant = 1.0;

float motorGain = 1.0;
float motorTrim = 0.0;

float k_r = (motorGain + motorTrim)/r_motorConstant;
float k_l = (motorGain - motorTrim)/l_motorConstant;
