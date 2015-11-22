#ifndef CLIENT_CONTROLLER_VIEW_H
#define CLIENT_CONTROLLER_VIEW_H

//-- includes -----
#include "ClientConfig.h"
#include <cassert>

//-- pre-declarations -----
struct PSMoveVector3;
struct PSMoveQuaternion;

namespace PSMoveProtocol
{
    class ControllerDataFrame;
};

//-- constants -----
enum PSMoveButtonState {
    PSMoveButton_UP = 0x00,       // (00b) Not pressed
    PSMoveButton_PRESSED = 0x01,  // (01b) Down for one frame only
    PSMoveButton_DOWN = 0x03,     // (11b) Down for >1 frame
    PSMoveButton_RELEASED = 0x02, // (10b) Up for one frame only
};

extern const PSMoveVector3 *k_psmove_vector3_zero;
extern const PSMoveQuaternion *k_psmove_quaternion_identity;

//-- declarations -----
struct PSMoveVector3
{
    float x, y, z;
};

struct PSMoveQuaternion
{
    float w, x, y, z;
};

struct PSMovePose
{
    PSMoveQuaternion Orientation;
    PSMoveVector3 Position;

    inline void Clear()
    {
        Orientation= *k_psmove_quaternion_identity;
        Position= *k_psmove_vector3_zero;
    }
};

class CLIENTPSMOVEAPI ClientControllerView
{
private:
    int PSMoveID;
    int SequenceNum;
    int ListenerCount;

    PSMovePose Pose;

    bool IsConnected;
    bool IsTrackingEnabled;
    bool IsCurrentlyTracking;

    PSMoveButtonState TriangleButton;
    PSMoveButtonState CircleButton;
    PSMoveButtonState CrossButton;
    PSMoveButtonState SquareButton;
    PSMoveButtonState SelectButton;
    PSMoveButtonState StartButton;
    PSMoveButtonState PSButton;
    PSMoveButtonState MoveButton;
    PSMoveButtonState TriggerButton;

    unsigned char PreviousTriggerValue;
    unsigned char TriggerValue;

    unsigned char CurrentRumble;
    unsigned char RumbleRequest;

    long long data_frame_last_received_time;
    float data_frame_average_fps;

public:
    ClientControllerView(int PSMoveID);

    void Clear();
    void ApplyControllerDataFrame(const PSMoveProtocol::ControllerDataFrame *data_frame);

    // Listener State
    inline void IncListenerCount()
    {
        ++ListenerCount;
    }

    inline void DecListenerCount()
    {
        assert(ListenerCount > 0);
        --ListenerCount;
    }

    inline int GetListenerCount() const
    {
        return ListenerCount;
    }

    // Controller Data Accessors
    inline int GetPSMoveID() const
    {
        return PSMoveID;
    }

    inline bool IsValid() const
    {
        return PSMoveID != -1;
    }

    inline bool GetIsConnected() const
    {
        return (IsValid() && IsConnected);
    }

    inline bool GetIsCurrentlyTracking() const
    {
        return GetIsConnected() ? IsCurrentlyTracking : false;
    }
    
    inline bool GetIsTrackingEnabled() const
    {
        return GetIsConnected() ? IsTrackingEnabled : false;
    }

    inline PSMoveVector3 GetPosition() const
    {
        return GetIsConnected() ? Pose.Position : *k_psmove_vector3_zero;
    }

    inline PSMoveQuaternion GetOrientation() const
    {
        return GetIsConnected() ? Pose.Orientation : *k_psmove_quaternion_identity;
    }
        
    inline PSMoveButtonState GetButtonTriangle() const
    {
        return GetIsConnected() ? TriangleButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonCircle() const
    {
        return GetIsConnected() ? CircleButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonCross() const
    {
        return GetIsConnected() ? CrossButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonSquare() const
    {
        return GetIsConnected() ? SquareButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonSelect() const
    {
        return GetIsConnected() ? SelectButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonStart() const
    {
        return GetIsConnected() ? StartButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonPS() const
    {
        return GetIsConnected() ? PSButton : PSMoveButton_UP;
    }
    
    inline PSMoveButtonState GetButtonMove() const
    {
        return GetIsConnected() ? MoveButton : PSMoveButton_UP;
    }

    inline PSMoveButtonState GetButtonTrigger() const
    {
        return GetIsConnected() ? TriggerButton : PSMoveButton_UP;
    }

    inline float GetTriggerValue() const
    {
        return GetIsConnected() ? ((float)TriggerValue / 255.f) : 0.f;
    }
    
    // Statistics
    inline float GetDataFrameFPS() const
    {
        return data_frame_average_fps;
    }
};
#endif // CLIENT_CONTROLLER_VIEW_H