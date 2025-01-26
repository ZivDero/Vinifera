/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          ROCKETLOCOMOTION.CPP
 *
 *  @authors       ZivDero
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
#include "viniferaflylocomotion.h"
#include "tibsun_inline.h"
#include "tibsun_globals.h"
#include "iomap.h"
#include "aircraftext.h"
#include "aircraft.h"
#include "aircrafttype.h"
#include "cell.h"
#include "anim.h"
#include "animtype.h"
#include "combat.h"
#include "coord.h"
#include "foot.h"
#include "tactical.h"
#include "wwmath.h"
#include "debughandler.h"
#include "extension.h"
#include "fastmath.h"
#include "vector2.h"
#include "voc.h"
#include "house.h"
#include "team.h"
#include "building.h"
#include "rules.h"
#include "weapontype.h"


/**
 *  Retrieves the class identifier (CLSID) of the object.
 * 
 *  @author: ZivDero
 */
IFACEMETHODIMP ViniferaFlyLocomotionClass::GetClassID(CLSID *pClassID)
{    
    if (pClassID == nullptr) {
        return E_POINTER;
    }

    *pClassID = __uuidof(this);

    return S_OK;
}


/**
 *  Initializes an object from the stream where it was saved previously.
 * 
 *  @author: ZivDero
 */
IFACEMETHODIMP ViniferaFlyLocomotionClass::Load(IStream *pStm)
{
    return LocomotionClass::Locomotion_Load(pStm);
}


/**
 *  Class default constructor.
 * 
 *  @author: ZivDero
 */
ViniferaFlyLocomotionClass::ViniferaFlyLocomotionClass() :
    LocomotionClass(),
    DestinationCoord(),
    HeadToCoord(),
    IsMoving(false),
    FlightLevel(0),
    TargetSpeed(0),
    CurrentSpeed(0),
    IsTakingOff(false),
    IsLanding(false),
    WasLanding(false),
    field_4B(false),
    field_4C_facing(0),
    field_50(0),
    IsElevating(false)
{
}


/**
 *  Sees if object is moving.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Is_Moving()
{
    return IsMoving || Linked_To()->PitchAngle > 0;
}


/**
 *  Fetches destination coordinate.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(Coordinate) ViniferaFlyLocomotionClass::Destination()
{
    if (IsMoving)
        return DestinationCoord;

    return Coordinate();
}


/**
 *  Fetch voxel draw matrix.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(Matrix3D) ViniferaFlyLocomotionClass::Draw_Matrix(int* key)
{
    return LocomotionClass::Draw_Matrix(key);
}


/**
 *  Fetch shadow draw matrix.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(Matrix3D) ViniferaFlyLocomotionClass::Shadow_Matrix(int* key)
{
    return LocomotionClass::Shadow_Matrix(key);
}


/**
 *  Draw point center location.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(Point2D) ViniferaFlyLocomotionClass::Draw_Point()
{
    return LocomotionClass::Draw_Point();
}


/**
 *  Shadow draw point center location.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(Point2D) ViniferaFlyLocomotionClass::Shadow_Point()
{
    return LocomotionClass::Shadow_Point();
}


/**
 *  Process movement of object.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Process()
{

    return Is_Moving();
}


/**
 *  Instruct to move to location specified.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(void) ViniferaFlyLocomotionClass::Move_To(Coordinate to)
{
    DestinationCoord = to;

    if (!DestinationCoord) {
        if (!HeadToCoord) {
            IsMoving = false;
        }

    }
    else {
        IsMoving = true;
    }
}


/**
 *  Stop moving at first opportunity.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(void) ViniferaFlyLocomotionClass::Stop_Moving()
{
    HeadToCoord = 0;
    DestinationCoord = 0;


    IsMoving = false;
}


/**
 *  Try to face direction specified.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(void) ViniferaFlyLocomotionClass::Do_Turn(DirStruct coord)
{
    Linked_To()->PrimaryFacing.Set(coord);
}


/**
 *  Locomotor loses power.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Power_Off()
{
    return LocomotionClass::Power_Off();
}


/**
 *  Is locomotor powered?
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Is_Powered()
{
    return LocomotionClass::Is_Powered();
}


/**
 *  Is locomotor sensitive to ion storms?
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Is_Ion_Sensitive()
{
    return false;
}


/**
 *  What display layer is it located in.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(LayerType) ViniferaFlyLocomotionClass::In_Which_Layer()
{
    return LAYER_GROUND;
}


/**
 *  Is it actually moving across the ground this very second?
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(bool) ViniferaFlyLocomotionClass::Is_Moving_Now()
{
    return CurrentSpeed != 0;
}


/**
 *  Actual current speed of object expressed as leptons per game frame.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(int) ViniferaFlyLocomotionClass::Apparent_Speed()
{
    return Linked_To()->Current_Speed();
}


/**
 *  Queries the general state of the locomotor.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(int) ViniferaFlyLocomotionClass::Get_Status()
{
    return 0;
}


/**
 *  Forces a hunter seeker droid to find a target.
 *
 *  @author: ZivDero
 */
IFACEMETHODIMP_(void) ViniferaFlyLocomotionClass::Acquire_Hunter_Seeker_Target()
{
}


/**
 *  Handle aircraft take off and landing processing.
 *
 *  @author: 07/29/1996 JLB - Created
 *           ZivDero - Adjustments for Tiberian Sun
 */
bool ViniferaFlyLocomotionClass::Landing_Takeoff_AI_499CA0()
{
    /**
     *  Handle landing and taking off logic. Helicopters are prime users of this technique. The
     *  aircraft will either gain or lose altitude as appropriate. As the aircraft transitions
     *  between flying level and ground level, it will be moved into the appropriate render
     *  layer.
     */
    if (Linked_To()->Strength > 0 && (IsLanding || IsTakingOff))
    {
        LayerType layer = Linked_To()->In_Which_Layer();
        Linked_To()->Mark(MARK_UP);
        Map.Remove(Linked_To());

        if (IsLanding)
            Process_Landing_49B360();

        if (IsTakingOff)
            Process_Take_Off_49B1A0();

        /**
         *  Make adjustments for altitude by moving from one layer to another as
         *  necessary.
         */
        if (layer != Linked_To()->In_Which_Layer())
        {
            /**
             *  When the aircraft is about to enter the ground layer, perform on last
             *  check to see if it is legal to enter that location. If not, then
             *  start the take off process. Let the normal logic handle this
             *  change of plans.
             */
            bool ok = true;
            if (Linked_To()->In_Which_Layer() == LAYER_GROUND && !IsTakingOff)
            {
                if (!Linked_To()->Is_LZ_Clear(&Map[Linked_To()->Get_Coord()]))
                {
                    IsTakingOff = true;
                    Linked_To()->IsOnBridge = false;
                    Linked_To()->Mark(MARK_UP);
                    Linked_To()->Set_Height(Linked_To()->Get_Height() + Pixel_To_Lepton(1) * 2);
                    Linked_To()->Mark(MARK_DOWN);
                    ok = false;
                }
            }

            if (ok)
            {
                /**
                 *  When the aircraft is close to the ground, it should exist as a ground object.
                 *  This aspect is controlled by the Place_Down and Pick_Up functions.
                 */
                if (Linked_To()->In_Which_Layer() == LAYER_GROUND)
                {
                    Linked_To()->Assign_Destination(nullptr);    // Clear the navcom.
                    Linked_To()->Transmit_Message(RADIO_TETHER);
                    Linked_To()->Look();
                }
                else
                {
                    Linked_To()->Transmit_Message(RADIO_UNTETHER);

                    /**
                     *  If the navigation computer is not attached to the object this
                     *  aircraft is in radio contact with, then assume that radio
                     *  contact is now superfluous. Break radio contact.
                     */
                    if (Linked_To()->In_Radio_Contact() && Target_Legal(Linked_To()->NavCom) && Linked_To()->NavCom != Linked_To()->Contact_With_Whom())
                        Linked_To()->Transmit_Message(RADIO_OVER_OUT);
                }
            }
        }
    }

    return false;
}


/**
 *  Detect if aircraft has exited the map.
 *
 *  @author: 07/29/1996 JLB - Created
 *           ZivDero - Adjustments for Tiberian Sun
 */
bool ViniferaFlyLocomotionClass::Edge_Of_World_AI_499E40()
{
    if (!Map.In_Local_Radar(Linked_To()->Get_Coord()))
    {
        if (Linked_To()->Mission == MISSION_RETREAT)
        {
            /**
             *  Check to see if there are any civilians aboard. If so, then flag the house
             *  that the civilian evacuation trigger event has been fulfilled.
             */
            while (Linked_To()->Cargo.Is_Something_Attached())
            {
                const FootClass* obj = Linked_To()->Cargo.Detach_Object();

                /**
                 *  Flag the owning house that civ evacuation has occurred.
                 */
                if (AircraftClass::Counts_As_Civ_Evac(obj))
                    obj->House->IsCivEvacuated = true;

                if (obj->Team)
                    obj->Team->IsLeaveMap = true;

                delete obj;
            }

            if (Linked_To()->Team)
                Linked_To()->Team->IsLeaveMap = true;
            
            Linked_To()->Stun();
            Linked_To()->Remove_This();
            return true;
        }
    }
    else
    {
        Linked_To()->IsLocked = true;
    }

    return false;
}


/**
 *  Handles aircraft physical movement logic.
 *
 *  @author: 07/29/1996 JLB - Created
 *           ZivDero - Adjustments for Tiberian Sun
 */
void ViniferaFlyLocomotionClass::Movement_AI_499F20()
{

}


/**
 *  Performs vector physics (movement).
 *
 *  @author: 04/24/1994 JLB - Created.
 *           06/05/1995 JLB - Simplified to just do movement.
 *           ZivDero - Adjustments for Tiberian Sun
 */
ImpactType ViniferaFlyLocomotionClass::Physics_49AFE0(Coordinate& coord, DirStruct facing)
{
    const int speed = Apparent_Speed();

    /**
     *  If movement occurred that is at least one
     *  pixel, then check update the coordinate and
     *  check for edge of world collision.
     */
    if (speed > 0)
    {
        Coordinate newcoord = Coord_Move(coord, facing, speed);

        /**
         *  Remember the new position.
         */
        coord = newcoord;

        /**
         *  If the new coordinate is off the edge of the world, then report
         *  this.
         */
        if (!Map.In_Radar(Coord_Cell(newcoord)))
            return IMPACT_EDGE;


        return IMPACT_NORMAL;
    }

    return IMPACT_NONE;
}


/**
 *  Handle aircraft body and flight rotation.
 *
 *  @author: ZivDero
 */
void ViniferaFlyLocomotionClass::Rotation_AI_49B0D0()
{
    if (field_4B)
    {
        if (Linked_To()->Strength > 0)
        {
            if (Is_Powered())
            {
                field_4B = false;
                Map[Linked_To()->Get_Coord()].Trigger_Veins();
            }
            else
            {
                Linked_To()->PrimaryFacing.Set(Linked_To()->PrimaryFacing.Current() + field_4C_facing / 32);
                const int thing = std::max(std::abs(field_4C_facing), 1);
                if (field_4C_facing >= 0)
                    field_4C_facing -= thing;
                else
                    field_4C_facing += thing;
            }
        }
    }

}


/**
 *  State machine support for taking off.
 *
 *  @author: 05/12/1995 JLB - Created
 *           ZivDero - Adjustments for Tiberian Sun
 */
bool ViniferaFlyLocomotionClass::Process_Take_Off_49B1A0()
{
    IsLanding = false;
    IsTakingOff = true;

    int height = Linked_To()->Get_Height();
    if (!Linked_To()->IsOnBridge)
    {
        if (Map[Linked_To()->Get_Coord()].Bit2_8 && height >= BRIDGE_HEIGHT)
            height -= BRIDGE_HEIGHT;
    }

    IFlyControl* flycontrol = Get_Fly_Control();

    int landing_altitude = 0;
    if (flycontrol)
        landing_altitude = flycontrol->Landing_Altitude();

    bool took_off = false;
    if (height >= FlightLevel)
    {
        IsTakingOff = false;
        IsLanding = false;
        took_off = true;
    }

    const int adjusted_height = height - landing_altitude;
    const int adjusted_flight_level = FlightLevel - landing_altitude;

    if (adjusted_height > adjusted_flight_level - (adjusted_flight_level / 3))
    {
        Linked_To()->SecondaryFacing.Set_Desired(Linked_To()->PrimaryFacing.Desired());
    }
    else if (adjusted_height > adjusted_flight_level / 2)
    {
        Coordinate center_coord = Linked_To()->Center_Coord();
        Linked_To()->PrimaryFacing.Set_Desired(Desired_Facing(DestinationCoord.X, DestinationCoord.Y, center_coord.X, center_coord.Y));
        TargetSpeed = 0;
    }

    flycontrol->Release();

    return took_off;
}


bool ViniferaFlyLocomotionClass::Process_Landing_49B360()
{
    if (!IsLanding)
        return true;

    if (Linked_To()->Techno_Type_Class()->IsHunterSeeker)
    {
        if (!Target_Legal(Linked_To()->TarCom))
        {
            Acquire_Hunter_Seeker_Target();
            if (Target_Legal(Linked_To()->TarCom))
            {
                IsLanding = false;
                FlightLevel = Linked_To()->Techno_Type_Class()->Flight_Level();
                if (Linked_To()->In_Radio_Contact())
                    Linked_To()->Transmit_Message(RADIO_OVER_OUT);
                Linked_To()->Assign_Mission(MISSION_ATTACK);
                Linked_To()->Commence();
                return false;
            }
        }
    }

    int height = Linked_To()->Get_Height();
    if (!Linked_To()->IsOnBridge)
    {
        if (Map[Linked_To()->Get_Coord()].Bit2_8 && height >= BRIDGE_HEIGHT)
            height -= BRIDGE_HEIGHT;
    }

    if (Linked_To()->Techno_Type_Class()->IsDropship && height == 0)
    {
        if (Linked_To()->PitchAngle > 0)
            Linked_To()->PitchAngle = std::max(Linked_To()->PitchAngle - 0.02f, 0.0f);
    }

    TargetSpeed = 0;

    IFlyControl* flycontrol = Get_Fly_Control();

    int landing_altitude = 0;
    if (flycontrol)
        landing_altitude = flycontrol->Landing_Altitude();

    bool can_land = false;
    if (Linked_To()->Can_Enter_Cell(&Map[DestinationCoord]))
    {
        Taking_Off_49CB00();

        Cell nearby_cell = Map.Nearby_Location(Coord_Cell(Linked_To()->Get_Coord()), SPEED_TRACK, -1, MZONE_FLYER);
        if (nearby_cell)
        {
            Coordinate coord = Cell_Coord(nearby_cell);
            coord.Z = Map.Get_Cell_Height(coord);
            if (Map[nearby_cell].Bit2_8)
                coord.Z += BRIDGE_HEIGHT;
            Move_To(coord);
            can_land = true;
        }
    }
    else
    {
        can_land = true;
    }

    if (can_land)
    {
        if (!WasLanding && height < 300)
        {
            Coordinate coord = Linked_To()->Get_Coord();
            coord.Z = Map.Get_Cell_Height(coord);
            WasLanding = true;

            if (Linked_To()->Techno_Type_Class()->IsDropship)
                new AnimClass(AnimTypeClass::As_Pointer(AnimTypeClass::From_Name("DROPLAND")), coord);
            else if (Linked_To()->What_Am_I() == RTTI_AIRCRAFT && static_cast<AircraftClass*>(Linked_To())->Class->IsCarryall)
                new AnimClass(AnimTypeClass::As_Pointer(AnimTypeClass::From_Name("CARYLAND")), coord);

            if (Linked_To()->Strength > 0)
                Sound_Effect(Linked_To()->Techno_Type_Class()->AuxSound2, coord);
        }

        if (height <= landing_altitude && Linked_To()->PitchAngle <= 0)
        {
            Coordinate coord = Linked_To()->Get_Coord();
            if (Map[coord].Bit2_8 && coord.Z >= Map.Get_Cell_Height(coord) + BRIDGE_HEIGHT)
                Linked_To()->IsOnBridge = true;

            Linked_To()->Set_Height(landing_altitude);
            IsLanding = false;
            IsTakingOff = false;
            Linked_To()->Set_Speed(0);
            CurrentSpeed = 0;
            TargetSpeed = 0;
            CurrentSpeed = 0;
            TargetSpeed = 0;

            if (Linked_To()->LastAdjanencyCell)
            {
                for (int i = 0; i < FACING_COUNT; i++)
                {
                    Cell adj_cell = Adjacent_Cell(Linked_To()->LastAdjanencyCell, static_cast<FacingType>(i));
                    Map[adj_cell].AdjacentObjectCount--;
                }

                Linked_To()->LastAdjanencyCell = Linked_To()->Get_Cell();

                for (int i = 0; i < FACING_COUNT; i++)
                {
                    Cell adj_cell = Adjacent_Cell(Linked_To()->LastAdjanencyCell, static_cast<FacingType>(i));
                    Map[adj_cell].AdjacentObjectCount++;
                }
            }
            else
            {
                Linked_To()->LastAdjanencyCell = Linked_To()->Get_Cell();

                for (int i = 0; i < FACING_COUNT; i++)
                {
                    Cell adj_cell = Adjacent_Cell(Linked_To()->LastAdjanencyCell, static_cast<FacingType>(i));
                    Map[adj_cell].AdjacentObjectCount++;
                }
            }

            if (Coord_Cell(DestinationCoord) == Coord_Cell(Linked_To()->Center_Coord())
                || Map[Coord_Cell(DestinationCoord)].Cell_Building() == Linked_To()->Radio)
            {
                IsMoving = false;
                Move_To(Coordinate());
                Linked_To()->Assign_Destination(nullptr);
            }
        }

        if (flycontrol)
            flycontrol->Release();

        return true;
    }

    Linked_To()->Take_Damage(Linked_To()->Strength, 0, Rule->C4Warhead, nullptr);
    DestinationCoord = Coordinate();

    if (flycontrol)
        flycontrol->Release();

    return false;
}


void ViniferaFlyLocomotionClass::Nearing_Target_49BBA0(bool a1, Coordinate coord)
{
    IFlyControl* flycontrol = Get_Fly_Control();

    if (flycontrol && Target_Legal(Linked_To()->TarCom) && Linked_To()->Ammo && flycontrol->Is_Strafe())
        a1 = false;

    Coordinate target_coord = coord;

    BuildingClass* building = Map[Coord_Cell(coord)].Cell_Building();
    if (building && !Linked_To()->Techno_Type_Class()->IsHunterSeeker)
        target_coord = building->Docking_Coord();

    int distance = (Linked_To()->Center_Coord().As_Cell() - target_coord.As_Cell()).Length();

    if (!flycontrol || !flycontrol->Is_Locked())
    {
        Coordinate center_coord = Linked_To()->Center_Coord();
        DirStruct facing = Desired_Facing(coord.X, coord.Y, center_coord.X, center_coord.Y);

        if (!flycontrol || !flycontrol->Is_Strafe()
            || (Linked_To()->Center_Coord().As_Cell() - coord.As_Cell()).Length() > 768
            || std::abs(facing.Get_Raw() - Linked_To()->PrimaryFacing.Current().Get_Raw()) <= 0x2000)
        {
            Linked_To()->PrimaryFacing.Set_Desired(facing);
        }
    }

    const FacingType landing_facing = flycontrol ? static_cast<FacingType>(flycontrol->Landing_Direction()) : FACING_FIRST;

    constexpr auto Set_Dir = [&](DirStruct& dir) -> void
        {
            if (!flycontrol || !flycontrol->Is_Locked())
                Linked_To()->SecondaryFacing.Set_Desired(dir);
        };

    if (distance < 256 && a1 && !Linked_To()->Techno_Type_Class()->IsHunterSeeker)
    {
        if (Target_Legal(Linked_To()->TarCom) && Linked_To()->Ammo)
        {
            if (!flycontrol || !flycontrol->Is_Strafe())
            {
                Set_Dir(Linked_To()->Direction(Linked_To()->TarCom));
            }
            else
            {
                Set_Dir(DirStruct(landing_facing));
            }
        }
        else
        {
            Set_Dir(DirStruct(landing_facing));
        }
        
    }
    else
    {
        Coordinate center_coord = Linked_To()->Center_Coord();
        Set_Dir(Desired_Facing(coord.X, coord.Y, center_coord.X, center_coord.Y));
    }

    if (Linked_To()->Techno_Type_Class()->IsHunterSeeker)
    {
        if (Target_Legal(Linked_To()->TarCom))
        {
            const int distance = (Linked_To()->Get_Coord().As_Cell() - DestinationCoord.As_Cell()).Length();
            if (distance < Rule->HunterSeekerDetonateProximity)
            {
                TechnoClass* techno = Target_As_Techno(Linked_To()->TarCom);
                if (techno)
                {
                    const auto weapon = Linked_To()->Get_Weapon(WEAPON_SLOT_PRIMARY)->Weapon;
                    int damage = weapon->Attack;
                    techno->Take_Damage(damage, 0, weapon->WarheadPtr, Linked_To(), true, true);
                    Linked_To()->Take_Damage(damage, 0, weapon->WarheadPtr, nullptr, true, true);
                    Do_Flash(Linked_To()->Get_Coord(), damage, Rule->C4Warhead, damage);
                }
            }
            else
            {
                
            }
        }
    }
}


void ViniferaFlyLocomotionClass::Taking_Off_49CB00()
{
    if (Linked_To()->EMPFramesRemaining > 0)
        return;

    IsLanding = false;
    IsTakingOff = true;
    FlightLevel = Linked_To()->Techno_Type_Class()->Flight_Level();

    if (!Linked_To()->Get_Height())
        Linked_To()->PrimaryFacing.Set(Linked_To()->SecondaryFacing.Desired());

    Sound_Effect(Linked_To()->Techno_Type_Class()->AuxSound1, Linked_To()->Get_Coord());
}


void ViniferaFlyLocomotionClass::Land_on_Airport_49CBA0()
{
    IsLanding = true;
    IsTakingOff = false;
    WasLanding = true;
    FlightLevel = 0;
}


bool ViniferaFlyLocomotionClass::Is_In_Flight_49CCE0()
{
    if (IsLanding)
        return false;

    if (!IsTakingOff)
        return true;

    if (Linked_To()->Get_Height() >= FlightLevel / 2)
        return true;

    return false;
}


int ViniferaFlyLocomotionClass::func_49CE70()
{
    field_4B = true;
    field_4C_facing = Random_Pick(20, 30);

    if (Random_Pick(0, 99) < 50)
        field_4C_facing = -field_4C_facing;

    return field_4C_facing;
}


bool ViniferaFlyLocomotionClass::Needs_To_Land_49D210()
{
    if (IsLanding || IsElevating)
        return true;

    IFlyControl* flycontrol = Get_Fly_Control();

    if ((flycontrol && !flycontrol->Is_Strafe()) || !Linked_To()->Ammo)
    {
        flycontrol->Release();
        return true;
    }

    flycontrol->Release();
    return false;
}


bool ViniferaFlyLocomotionClass::Is_Locked_To_Straight_Flight_49D2D0()
{
    if (DestinationCoord.Z > Map.Get_Cell_Height(DestinationCoord) + 120)
        return true;

    if (Linked_To()->TarCom && Linked_To()->Ammo)
        return true;

    IFlyControl* flycontrol = Get_Fly_Control();

    if (!flycontrol)
        return false;

    if (!flycontrol->Is_Locked())
    {
        flycontrol->Release();
        return false;
    }

    flycontrol->Release();
    return true;
}
