**Date : 30 july 2026
**
*****Completed the automation for building and deploying the responder unit application from QT Creator****
1. Previously project opened on Desktop Kit So QT Creator builts only x86 executable for PC.
because of that ,after every change in header or source code 
a.Build project manually 
b.Build arm project manually
c.Copy executable code to STM32MP board separtely.
d.Execute code or run code manually on board .

#### To automate this i configured by adding custom process step in build setting .This step run the shell scrip "run_to_rebuild.sh" after the build this script performs following actions :
a.Sources the STM32MP1 QT SDK environment.
b.Build the arm executable (Build-arm)
c.Copies executable generated code to the STM32MP1 board using SCP command 
d.Stops previously running application.
e.Lanuches the updated application on the board.

$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$


1. Health Page
Implemented Health Page functionality.
Displayed ETBU health/online status.
Updated communication status based on received data.
Verified health information updates in the UI.


2. Settings Page
Implemented Settings Page.
Added configuration options required for the Response Unit.
Connected UI controls with backend logic.
Verified settings are reflected correctly in the application.


3. Talk Command Parsing
Implemented parsing for Talk command received from ETBU.
Decoded function codes received over RS-485.
Updated ETBU state based on received commands.
Connected parsed commands with application workflow.


4. GPS Integration
Integrated GPS module into the Qt application.
Opened GPS serial port using QSerialPort.
Read NMEA sentences from the GPS receiver.
Verified GPS data reception through debug logs.
Connected GPS module with the application backend.


5. Code Integration & Testing
Integrated new modules into the existing Qt project.
Performed communication testing.
Fixed minor parsing and integration issues.
Verified successful build and execution.


Files Modified
mainwindow.cpp
mainwindow.h
master_communication.cpp
gpsmanager.cpp
gpsmanager.h
Related UI files

**Date : 31 july 2026
**
1. Added Section-5 Response Unit frame support

Added support for the following RDSO Response Unit function codes:

0x8D – Active Request
0x8E – Active (Health Poll)
0x8F – Inactive Acknowledgement

Implemented frame creation functions:

createResponseUnitActiveRequest()
createResponseUnitActive()
createResponseUnitInactive()


2. Modified receive logic
Updated readResponse() to distinguish between:
ETBU communication (0x00–0x03)
Response Unit communication (0x8D–0x8F)
Added routing of Response Unit frames to:
handleResponseUnitFrame(srcAddress,
                        destAddress,
                        functionCode);

instead of processing them as ETBU frames.

3. Added Response Unit state machine
Implemented handling of:
Active Request (0x8D)
Active Poll (0x8E)
Inactive ACK (0x8F)
inside
handleResponseUnitFrame()


4. Added Master/Slave role switching
Implemented logic for:
Master → Slave transition
Slave → Master transition
Updated
m_isMaster
during role changes.

5. Updated Response Unit address
Updated
responderAddress
during role switching.
Example:
MASTER -> responderAddress = 0x01
SLAVE  -> responderAddress = 0x02
6. Added Master/Slave polling control
Modified
startCommunication()
so that

Master:

pollTimer->start(50);
peerPollTimer->start(1000);

Slave:

No ETBU polling
Waits for Active Poll from Master


7. Restricted ETBU operations to Master only
Added checks in:
pollDevice()
answerCall()
holdCall()
resumeCall()
endCall()

Example:

if(!m_isMaster)
{
    qDebug() << "Slave cannot answer ETBU call.";
    return;
}

8. Added detailed debug logs
Added logs showing:
Source Address
Destination Address
Function Code
Current Role
Current Address

to simplify debugging of Response Unit communication.


9. Tested with Python Response Unit Emulator
Verified successful communication:

Python(Master)
      |
      | 0x8E
      |
      v
Qt(Slave)

Qt
      |
      | 0x8F
      |
      v
Python

**Date : 01 August 2026
**
Emergency Talk Back Unit (ETBU) - Qt Response Unit

Work Done
1. Completed Master-Slave Role Switching Logic

    Implemented complete Response Unit role switching according to the protocol.

    Added handling for:

        0x8D – Active Request

        0x8E – Active Poll

        0x8F – Inactive / Acknowledgement

    Updated handleResponseUnitFrame() to process Master-Slave communication correctly.

2. Added Destination Address Validation

    Added destination address checking before processing Response Unit frames.

    Frames addressed to another Response Unit are ignored.

if(destAddress != myAddress)
{
    qDebug() << "Frame not addressed to this unit. Ignoring...";
    return;
}

3. Dynamic Response Unit Address Update

    Updated responderAddress automatically whenever the unit changes its role.

When promoted to Master:

m_isMaster = true;
responderAddress = MASTER_RU_ADDRESS;

When demoted to Slave:

m_isMaster = false;
responderAddress = SLAVE_RU_ADDRESS;

4. Improved Debug Logging

Added detailed debug messages showing:

    Source Address

    Destination Address

    Function Code

    Current Role

    Current Response Unit Address

    Role change status (Master/Slave)

This makes communication debugging easier.
5. Updated Master/Slave Communication Flow

Verified the complete sequence:

    Slave sends 0x8D (Active Request)

    Master receives 0x8D

    Master replies with 0x8F

    Master changes to Slave

    Slave receives 0x8F

    Slave changes to Master

    New Master starts ETBU polling

    New Master starts periodic 0x8E health polling

6. Tested with Python Response Unit Emulator

Successfully verified:

    Active Poll (0x8E)

    Active Request (0x8D)

    Inactive Acknowledgement (0x8F)

    Automatic Master promotion

    Automatic Slave demotion

    ETBU polling after Master promotion

    ETBU responses received correctly

7. Identified Startup Issue

Observed that requestBecomeMaster() was called before the serial port was opened.

Error observed:

QIODevice::write (QSerialPort): device not open

Confirmed that the serial port must be opened before sending the Active Request.
Files Modified

    master_communication.cpp

    master_communication.h

Result

     Master-Slave Response Unit communication implemented.

     Automatic role switching working correctly.

     ETBU polling starts automatically after becoming Master.

     Python emulator successfully communicates with the Qt Response Unit.

     Added detailed debug logs for easier troubleshooting.


**Date : 04 August 2026
**

1.Implemented Master-Slave communication between two Response Units.

2.Added Active Poll (0x8E) communication between Master and Slave.

3.Implemented Active Request (0x8D) and Inactive Confirmation (0x8F) for Master handover.

4.Added a 30-second timeout mechanism to detect Master communication failure.

5.Implemented automatic Slave-to-Master fail-over after timeout.

6.Tested communication using the Qt Response Unit application and Python emulator.

7.Debugged communication logs and verified frame transmission and reception.
 
