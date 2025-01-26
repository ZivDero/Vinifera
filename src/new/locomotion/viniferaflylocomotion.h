/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          ROCKETLOCOMOTION.H
 *
 *  @authors       CCHyper
 *
 *  @brief         Rocket locomotion implementation.
 *
 *  @license       Vinifera is free software: you can redistribute it and/or
 *                 modify it under the terms of the GNU General Public License
 *                 as published by the Free Software Foundation, either version
 *                 3 of the License, or (at your option) any later version.
 *
 *                 Vinifera is distributed in the hope that it will be
 *                 useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *                 PURPOSE. See the GNU General Public License for more details.
 *
 *                 You should have received a copy of the GNU General Public
 *                 License along with this program.
 *                 If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#pragma once

#include "always.h"
#include "iflycontrol.h"
#include "locomotion.h"
#include "rockettype.h"
#include "foot.h"
#include "vinifera_defines.h"



class DECLSPEC_UUID(CLSID_FLY_LOCOMOTOR)
ViniferaFlyLocomotionClass : public LocomotionClass
{
public:
    /**
     *  IPersist
     */
    IFACEMETHOD(GetClassID)(CLSID* pClassID) override;

    /**
     *  IPersistStream
     */
    IFACEMETHOD(Load)(IStream* pStm) override;

    /**
     *  LocomotionClass
     */
    int Size_Of(bool firestorm = false) const override { return sizeof(*this); }

    /**
     *  ILocomotion
     */
    IFACEMETHOD_(bool, Is_Moving)() override;
    IFACEMETHOD_(Coordinate, Destination)() override;
    IFACEMETHOD_(Matrix3D, Draw_Matrix)(int* key) override;
    IFACEMETHOD_(Matrix3D, Shadow_Matrix)(int* key) override;
    IFACEMETHOD_(Point2D, Draw_Point)() override;
    IFACEMETHOD_(Point2D, Shadow_Point)() override;
    IFACEMETHOD_(bool, Process)() override;
    IFACEMETHOD_(void, Move_To)(Coordinate to) override;
    IFACEMETHOD_(void, Stop_Moving)() override;
    IFACEMETHOD_(void, Do_Turn)(DirStruct coord) override;
    IFACEMETHOD_(bool, Power_Off)() override;
    IFACEMETHOD_(bool, Is_Powered)() override;
    IFACEMETHOD_(bool, Is_Ion_Sensitive)() override;
    IFACEMETHOD_(LayerType, In_Which_Layer)() override;
    IFACEMETHOD_(bool, Is_Moving_Now)() override;
    IFACEMETHOD_(int, Apparent_Speed)() override;
    IFACEMETHOD_(int, Get_Status)() override;
    IFACEMETHOD_(void, Acquire_Hunter_Seeker_Target)() override;

    ViniferaFlyLocomotionClass();
    ~ViniferaFlyLocomotionClass() override = default;

private:
    /**
     *  ViniferaFlyLocomotionClass
     */
    bool Landing_Takeoff_AI_499CA0();
    bool Edge_Of_World_AI_499E40();
    void Movement_AI_499F20();
    ImpactType Physics_49AFE0(Coordinate& coord, DirStruct facing);
    void Rotation_AI_49B0D0();
    bool Process_Take_Off_49B1A0();
    bool Process_Landing_49B360();
    void Nearing_Target_49BBA0(bool a1, Coordinate coord);
    void Taking_Off_49CB00();
    void Land_on_Airport_49CBA0();
    bool Is_In_Flight_49CCE0();
    int func_49CE70();
    bool Needs_To_Land_49D210();
    bool Is_Locked_To_Straight_Flight_49D2D0();

    inline IFlyControl* Get_Fly_Control()
    {
        IFlyControl* flycontrol = nullptr;
        const HRESULT hr = Linked_To()->QueryInterface(__uuidof(IFlyControl), reinterpret_cast<LPVOID*>(&flycontrol));

        if (FAILED(hr) && hr != E_NOINTERFACE)
            _com_issue_error(hr);

        return flycontrol;
    }


public:
    ViniferaFlyLocomotionClass(const ViniferaFlyLocomotionClass&) = delete;
    ViniferaFlyLocomotionClass& operator=(const ViniferaFlyLocomotionClass&) = delete;
    
protected:
    Coordinate DestinationCoord;
    Coordinate HeadToCoord;
    bool IsMoving;
    int FlightLevel;
    double TargetSpeed;
    double CurrentSpeed;
    bool IsTakingOff;
    bool IsLanding;
    bool WasLanding;
    bool field_4B;
    int field_4C_facing;
    int field_50;
    bool IsElevating;
};
