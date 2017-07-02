/* OpenLoopControl.ino

    Contributors: Roger Kassouf, Iwan Martin, Chen Liang, Aaron Pycraft

    Date last modified: June 26, 2017

    Objective: This sketch maps PPM inputs from the receiver to servo outputs 
               for each motor.

    INPUTS: Throttle, Roll, Pitch, Yaw
    OUTPUTS: Speeds of motors 1, 2, 3, 4
*/

#include <Servo.h>
#include <Wire.h>
#include <TimerOne.h>
#include "PinDefinitions.h"
#include <MPU9250_RegisterMap.h>
#include <SparkFunMPU9250-DMP.h>
#include <PID_v1.h>


//--Global variables
  //--Used as main control signal from tx input to motor speed settings. 
  unsigned int *quadSignal; //although this is global scope; we'll pass as argument
  unsigned int *motorSpeeds; // used as input to motor control
  double *gyroSignal;
  double maxSpeedCommand = 10; // dps
  const bool ENABLE_MOTORS = true;
  bool TxSignalError = false; //true if error exists in Tx
  bool skipControlTransfer = false;
  // Definitions of indexes in txSignal. ki for "const index"
  const int THROTTLE = 2; 
  const int PITCH    = 1;
  const int ROLL     = 0;
  const int YAW      = 3;

  float gyroX = 0;
  float gyroY = 0;
  float gyroZ = 0;

  MPU9250_DMP imu;  

  // Control Loop Configuration
  unsigned int cntrLoopConfig = 2; // 0 for throttle-only, 1 for open loop, 2 for closed loop.

  //Define Variables we'll be connecting to
  double pitchSetpoint, pitchInput, pitchOutput;
  double rollSetpoint, rollInput, rollOutput;
  double yawSetpoint, yawInput, yawOutput;

  //Specify the links and initial tuning parameters
  double pitchKp=1, pitchKi=0, pitchKd=1;
  double rollKp=1, rollKi=0, rollKd=1;
  double yawKp=1, yawKi=0, yawKd=1;

  // Declare PID objects
  PID pitchPID(&pitchInput, &pitchOutput, &pitchSetpoint, pitchKp, pitchKi, pitchKd, DIRECT);
  PID rollPID(&rollInput, &rollOutput, &rollSetpoint, rollKp, rollKi, rollKd, DIRECT);
  PID yawPID(&yawInput, &yawOutput, &yawSetpoint, yawKp, yawKi, yawKd, DIRECT);

  bool toggle = 1; // PID loops are 1: ON, 0: OFF

                        
//--Setup to run once. 
void setup() {
  Serial.begin(115200);

  configIMU();
  
  //--Setup transmitter decoding
  clearTxCycleFlag(); // true at the end of each tx signal cycle
  initTransmitterDecoding();
  
  quadSignal = new unsigned int[4];
  motorSpeeds = new unsigned int[4];
  gyroSignal = new double[3];
  
  if(ENABLE_MOTORS) {
    initMotorControl();
    setMotorsToMin();
  }
  while(millis() < 16000){
    // Wait until Gyro has finished calibrating
  }
  
  Serial.println("setup complete");

  pitchPID.SetOutputLimits(-100,100);
  rollPID.SetOutputLimits(-100,100);
  yawPID.SetOutputLimits(-100,100);

  // Currently set for 10 ms, see if 1ms works later
  pitchPID.SetSampleTime(10);
  rollPID.SetSampleTime(10);
  yawPID.SetSampleTime(10);
  
}

//--Main program loop
void loop() {
  //--Get input from transmitter
  getTxInput(quadSignal);
  if(TxSignalError) {
    Serial.print("Tx signal lost!..."); Serial.println(micros());
    setMotorsToMin();
    skipControlTransfer = true;
  } else  {
    //printTxSignals(quadSignal); // uncomment to see tx signals
  }

  if(!skipControlTransfer){
  // Update based on IMU
    if ( imu.fifoAvailable() )
  {
    // Use dmpUpdateFifo to update the ax, gx, mx, etc. values
    if ( imu.dmpUpdateFifo() == INV_SUCCESS)
    {
      updateGyro(); // Get updated Gyro data
      printGyroValues(); // Uncomment to print raw gyro values
    }
    else {
    Serial.print("DMP Update Fifo error"); Serial.println(micros());
    }
  }
    
  }

   if (toggle) {
    pitchPID.SetMode(AUTOMATIC);
    rollPID.SetMode(AUTOMATIC);
    yawPID.SetMode(AUTOMATIC);
   }
   else {
    pitchPID.SetMode(MANUAL);
    rollPID.SetMode(MANUAL);
    yawPID.SetMode(MANUAL);    
   }
 
  // Update the inputs signals
  pitchInput = 100*gyroSignal[0]/maxSpeedCommand;
  rollInput = 100*gyroSignal[1]/maxSpeedCommand;
  yawInput = 100*gyroSignal[2]/maxSpeedCommand;

  // Update the setpoints
  pitchSetpoint = (double) quadSignal[PITCH];
  rollSetpoint = (double) quadSignal[ROLL];
  yawSetpoint = (double) quadSignal[YAW];  

  // Compute the new output
  pitchPID.Compute();
  rollPID.Compute();
  yawPID.Compute();
 
  //--Tranform Tx signal to motor speed settings.
  //--TODO: implement new control algorithm
  if(skipControlTransfer) {
    skipControlTransfer = false;
  } else {
    controlTransfer(quadSignal, motorSpeeds, gyroSignal, 2); 
  }
  //printMotorValues(motorSpeeds);
  
  //--Send signal to motors, if no errors exist && they're enabled
  //--TODO; check that this logical check is working correctly
  if(!TxSignalError && ENABLE_MOTORS) {
    powerMotors(motorSpeeds);
  }

}

//--Probably unnecessary b/c quad will rarely be shutdown in a civil manner.
//  included for best practice.
void destroy() {
  delete quadSignal;
}

//--Prints processed TX input values.
void printTxSignals(unsigned int *txSignal) {
  //--Note: something messes up character encoding when trying to print 
  //  different data types at once. To print, follow following format:
  Serial.print("Throttle: "); Serial.print(txSignal[THROTTLE]); Serial.print("\t");
  Serial.print("Roll: \t");   Serial.print(txSignal[ROLL]);     Serial.print("\t");
  Serial.print("Pitch: ");    Serial.print(txSignal[PITCH]);    Serial.print("\t");
  Serial.print("Yaw: \t");    Serial.print(txSignal[YAW]);      Serial.print("\t");
  Serial.println();
}

//--Prints motor output values.
void printMotorValues(unsigned int *motorsOut) { 
  Serial.print("Motor 1: "); Serial.print(motorsOut[0]); Serial.print('\t');
  Serial.print("Motor 2: "); Serial.print(motorsOut[1]); Serial.print('\t');
  Serial.print("Motor 3: "); Serial.print(motorsOut[2]); Serial.print('\t');
  Serial.print("Motor 4: "); Serial.print(motorsOut[3]); Serial.print('\t');
  Serial.println();
}

//--Utility to quickly set values to zero
void clearQuadSignal() {
  quadSignal[0] = 0;
  quadSignal[1] = 0;
  quadSignal[2] = 0;
  quadSignal[3] = 0;
}


void printGyroValues(){
    
    Serial.println("Gyro: " + String(gyroX) + ", " +
              String(gyroY) + ", " + String(gyroZ) + " dps");
}

//void togglePIDs(bool toggle, unsigned int *pitchRef) {
//  /* togglePIDs will either enable or disable the PID loops, based upon the value of the
//   *  toggle: false for off, and true for on.
//   */
//   
//}
//
//void runPIDs(unsigned int &txSignal, double &gyroSignal, double maxSpeedCommand) {
//  /* runPIDs will compute the next output from previous states iteratively.
//   *  
//   */
//   
//
//}

