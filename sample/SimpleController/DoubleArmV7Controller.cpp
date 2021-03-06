#include <cnoid/SimpleController>
#include <cnoid/Joystick>

using namespace std;
using namespace cnoid;

class DoubleArmV7Controller : public cnoid::SimpleController
{
    Body* body;
    double dt;

    vector<int> armJointIdMap;
    vector<Link*> armJoints;
    vector<double> qref;
    vector<double> qold;
    vector<double> pgain;
    vector<double> dgain;
    double trackgain;

    Link::ActuationMode mainActuationMode;

    enum AxisType { STICK, BUTTON };

    struct OperationAxis {
        Link* joint;
        AxisType type;
        int id;
        double ratio;
        int shift;
        OperationAxis(Link* joint, AxisType type, int id, double ratio, int shift = 0)
            : joint(joint), type(type), id(id), ratio(ratio), shift(shift) { }
    };

    vector<vector<OperationAxis>> operationAxes;
    int operationSetIndex;
    bool prevSelectButtonState;

    const int SHIFT_BUTTON = Joystick::L_BUTTON;
    int shiftState;
    
    const int SELECT_BUTTON_ID = Joystick::LOGO_BUTTON;
    
    Link* trackL;
    Link* trackR;
    bool hasPseudoContinuousTracks;
    bool hasContinuousTracks;

    Joystick joystick;

    Link* link(const char* name) { return body->link(name); }

public:

    DoubleArmV7Controller();
    virtual bool initialize(SimpleControllerIO* io) override;
    bool initPseudoContinuousTracks(SimpleControllerIO* io);
    bool initContinuousTracks(SimpleControllerIO* io);
    void initArms(SimpleControllerIO* io);
    void initPDGain();
    void initJoystickKeyBind();
    virtual bool control() override;
    void controlTracks();
    void setTargetArmPositions();
    void controlArms();
    void controlArmsWithTorque();
    void controlArmsWithVelocity();
    void controlArmsWithPosition();
};

DoubleArmV7Controller::DoubleArmV7Controller()
{
    mainActuationMode = Link::ActuationMode::JOINT_TORQUE;
    hasPseudoContinuousTracks = false;
    hasContinuousTracks = false;
}

bool DoubleArmV7Controller::initialize(SimpleControllerIO* io)
{
    body = io->body();
    dt = io->timeStep();

    string option = io->optionString();
    if(option == "velocity")        mainActuationMode = Link::ActuationMode::JOINT_VELOCITY;
    else if(option  == "position")  mainActuationMode = Link::ActuationMode::JOINT_ANGLE;
    else                            mainActuationMode = Link::ActuationMode::JOINT_TORQUE;


    if(!initPseudoContinuousTracks(io))
        initContinuousTracks(io);
    initArms(io);
    initPDGain();
    initJoystickKeyBind();
    return true;
}

bool DoubleArmV7Controller::initPseudoContinuousTracks(SimpleControllerIO* io)
{
    trackL = body->link("TRACK_L");
    trackR = body->link("TRACK_R");
    if(!trackL) return false;
    if(!trackR) return false;

    if(trackL->actuationMode() == Link::JOINT_SURFACE_VELOCITY &&
    trackR->actuationMode() == Link::JOINT_SURFACE_VELOCITY   ){
        io->enableOutput(trackL);
        io->enableOutput(trackR);
        hasPseudoContinuousTracks = true;
        return true;
    }
    return false;
}

bool DoubleArmV7Controller::initContinuousTracks(SimpleControllerIO* io)
{
    trackL = body->link("WHEEL_L0");
    trackR = body->link("WHEEL_R0");
    if(!trackL) return false;
    if(!trackR) return false;

    if(mainActuationMode == Link::ActuationMode::JOINT_TORQUE){
        trackL->setActuationMode(Link::ActuationMode::JOINT_TORQUE);
        trackR->setActuationMode(Link::ActuationMode::JOINT_TORQUE);
    }else{
        trackL->setActuationMode(Link::ActuationMode::JOINT_VELOCITY);
        trackR->setActuationMode(Link::ActuationMode::JOINT_VELOCITY);
    }
    io->enableOutput(trackL);
    io->enableOutput(trackR);
    hasContinuousTracks = true;
    return true;
}

#include <iostream>
void DoubleArmV7Controller::initArms(SimpleControllerIO* io)
{
    for(auto joint : body->joints()){
        if(joint->jointId() >= 0 && (joint->isRevoluteJoint() || joint->isPrismaticJoint())){
            joint->setActuationMode(mainActuationMode);
            io->enableIO(joint);
            armJointIdMap.push_back(armJoints.size());
            armJoints.push_back(joint);
            qref.push_back(joint->q());
        } else {
            armJointIdMap.push_back(-1);
        }
    }
    qold = qref;
}

void DoubleArmV7Controller::initPDGain()
{
    // Tracks
    if(hasPseudoContinuousTracks) trackgain = 1.0;
    if(hasContinuousTracks){
        if(mainActuationMode == Link::ActuationMode::JOINT_TORQUE){
            trackgain = 2000.0;
        }else{
            trackgain = 2.0;
        }
    }

    // Arm
    if(mainActuationMode == Link::ActuationMode::JOINT_TORQUE){
        pgain = {
        /* MFRAME */ 200000, /* BLOCK */ 150000, /* BOOM */ 150000, /* ARM  */ 100000,
        /* PITCH  */  30000, /* ROLL  */  20000, /* TIP1 */    500, /* TIP2 */    500,
        /* UFRAME */ 150000, /* SWING */  50000, /* BOOM */ 100000, /* ARM  */  80000,
        /* ELBOW */   30000, /* YAW   */  20000, /* HAND */    500, /* ROD  */  50000};
        dgain = {
        /* MFRAME */ 20000, /* BLOCK */ 15000, /* BOOM */ 10000, /* ARM  */ 5000,
        /* PITCH  */   500, /* ROLL  */   500, /* TIP1 */    50, /* TIP2 */   50,
        /* UFRAME */ 15000, /* SWING */  1000, /* BOOM */  3000, /* ARM  */ 2000,
        /* ELBOW */    500, /* YAW   */   500, /* HAND */    20, /* ROD  */ 5000};
    }
    if(mainActuationMode == Link::ActuationMode::JOINT_VELOCITY){
        pgain = {
        /* MFRAME */ 100, /* BLOCK */ 180, /* BOOM */ 150, /* ARM  */ 100,
        /* PITCH  */  30, /* ROLL  */  20, /* TIP1 */   5, /* TIP2 */   5,
        /* UFRAME */ 150, /* SWING */ 180, /* BOOM */ 100, /* ARM  */  80,
        /* ELBOW */   30, /* YAW   */  20, /* HAND */   1, /* ROD  */  50};
    }
    if(mainActuationMode == Link::ActuationMode::JOINT_ANGLE){
    }
}

void DoubleArmV7Controller::initJoystickKeyBind()
{
    operationAxes = {
        {
            { link("MFRAME"),       STICK,  Joystick::L_STICK_H_AXIS, -0.6 },
            { link("BLOCK"),        STICK,  Joystick::R_STICK_H_AXIS, -0.6 },
            { link("BOOM"),         STICK,  Joystick::L_STICK_V_AXIS, -0.6 },
            { link("ARM"),          STICK,  Joystick::R_STICK_V_AXIS,  0.6 },
            { link("TOHKU_PITCH"),  BUTTON, Joystick::A_BUTTON,        0.6 },
            { link("TOHKU_PITCH"),  BUTTON, Joystick::Y_BUTTON,       -0.6 },
            { link("TOHKU_ROLL"),   BUTTON, Joystick::X_BUTTON,        1.0 },
            { link("TOHKU_ROLL"),   BUTTON, Joystick::B_BUTTON,       -1.0 },
            { link("TOHKU_TIP_01"), STICK,  Joystick::R_TRIGGER_AXIS, -0.6 },
            { link("TOHKU_TIP_02"), STICK,  Joystick::R_TRIGGER_AXIS, -0.6 },
            { link("TOHKU_TIP_01"), BUTTON, Joystick::R_BUTTON,        0.5 },
            { link("TOHKU_TIP_02"), BUTTON, Joystick::R_BUTTON,        0.5 }
        },
        {
            { link("UFRAME"),       STICK,  Joystick::L_STICK_H_AXIS, -0.6 },
            { link("MNP_SWING"),    STICK,  Joystick::R_STICK_H_AXIS, -0.6 },
            { link("MANIBOOM"),     STICK,  Joystick::L_STICK_V_AXIS, -0.6 },
            { link("MANIARM"),      STICK,  Joystick::R_STICK_V_AXIS,  0.6 },
            { link("MANIELBOW"),    BUTTON, Joystick::A_BUTTON,        0.6 },
            { link("MANIELBOW"),    BUTTON, Joystick::Y_BUTTON,       -0.6 },
            { link("YAWJOINT"),     BUTTON, Joystick::X_BUTTON,        1.0, 1 },
            { link("YAWJOINT"),     BUTTON, Joystick::B_BUTTON,       -1.0, 1 },
            { link("HANDBASE"),     BUTTON, Joystick::X_BUTTON,       -1.0, 0 },
            { link("HANDBASE"),     BUTTON, Joystick::B_BUTTON,        1.0, 0 },
            { link("PUSHROD"),      STICK,  Joystick::R_TRIGGER_AXIS, -0.04 },
            { link("PUSHROD"),      BUTTON, Joystick::R_BUTTON,        0.04 },
        }
    };

    operationSetIndex = 0;
    prevSelectButtonState = false;
}

bool DoubleArmV7Controller::control()
{
    joystick.readCurrentState();

    shiftState = joystick.getButtonState(SHIFT_BUTTON) ? 1 : 0;

    bool selectButtonState = joystick.getButtonState(SELECT_BUTTON_ID);
    if(!prevSelectButtonState && selectButtonState){
        operationSetIndex = 1 - operationSetIndex;
    }
    prevSelectButtonState = selectButtonState;

    trackL->u() = 0.0;
    trackL->dq() = 0.0;
    trackR->u() = 0.0;
    trackR->dq() = 0.0;
    controlTracks();
    setTargetArmPositions();
    controlArms();

    return true;
}

void DoubleArmV7Controller::controlTracks()
{
    double pos[2];

    const double k1 = 0.4;
    const double k2 = 0.6;

    pos[0] = k1 * joystick.getPosition(Joystick::DIRECTIONAL_PAD_H_AXIS);
    pos[1] = k1 * joystick.getPosition(Joystick::DIRECTIONAL_PAD_V_AXIS);
    
    if(shiftState == 1){
        pos[0] += k2 * (
            joystick.getPosition(Joystick::L_STICK_H_AXIS) +
            joystick.getPosition(Joystick::R_STICK_H_AXIS));
        pos[1] += k2 * (
            joystick.getPosition(Joystick::L_STICK_V_AXIS) +
            joystick.getPosition(Joystick::R_STICK_V_AXIS));
    }
    
    for(int i=0; i < 2; ++i){
        if(fabs(pos[i]) < 0.2){
            pos[i] = 0.0;
        }
    }

    // set the velocity of each track
    if(hasPseudoContinuousTracks || mainActuationMode == Link::ActuationMode::JOINT_VELOCITY
        || mainActuationMode == Link::ActuationMode::JOINT_ANGLE){
        trackL->dq() = trackgain * (-2.0 * pos[1] + pos[0]);
        trackR->dq() = trackgain * (-2.0 * pos[1] - pos[0]);
    }else if(mainActuationMode == Link::ActuationMode::JOINT_TORQUE){
        trackL->u() = trackgain * (-2.0 * pos[1] + pos[0]);
        trackR->u() = trackgain * (-2.0 * pos[1] - pos[0]);
    }
}

void DoubleArmV7Controller::setTargetArmPositions()
{
    const vector<OperationAxis>& axes = operationAxes[operationSetIndex];

    for(auto& axis : axes){
        if(axis.shift < 0 || axis.shift == shiftState){
            Link* joint = axis.joint;
            double& q = qref[armJointIdMap[joint->jointId()]];
            if(axis.type == BUTTON){
                if(joystick.getButtonState(axis.id)){
                    q += axis.ratio * dt;
                }
            } else if(axis.type == STICK){
                double pos = joystick.getPosition(axis.id);
                q += axis.ratio * pos * dt;
            }
            if(q > joint->q_upper()){
                q = joint->q_upper();
            } else if(q < joint->q_lower()){
                q = joint->q_lower();
            }
        }
    }
}

void DoubleArmV7Controller::controlArms()
{
    if(mainActuationMode == Link::ActuationMode::JOINT_VELOCITY){
        controlArmsWithVelocity();
    }else if(mainActuationMode == Link::ActuationMode::JOINT_ANGLE){
        controlArmsWithPosition();
    }else{
        controlArmsWithTorque();
    }
}

void DoubleArmV7Controller::controlArmsWithTorque()
{
    for(size_t i=0; i < armJoints.size(); ++i){
        Link* joint = armJoints[i];
        double q = joint->q();
        double dq = (q - qold[i]) / dt;
        joint->u() = (qref[i] - q) * pgain[i] + (0.0 - dq) * dgain[i];
        qold[i] = q;
    }
}

void DoubleArmV7Controller::controlArmsWithVelocity()
{
    for(size_t i=0; i < armJoints.size(); ++i){
        Link* joint = armJoints[i];
        double q = joint->q();
        joint->dq() = (qref[i] - q) * pgain[i];
    }
}

void DoubleArmV7Controller::controlArmsWithPosition()
{
    for(size_t i=0; i < armJoints.size(); ++i){
        Link* joint = armJoints[i];
        joint->q() = qref[i];
    }
}


CNOID_IMPLEMENT_SIMPLE_CONTROLLER_FACTORY(DoubleArmV7Controller)
