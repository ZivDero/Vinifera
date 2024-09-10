/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          TIBERIUMEXT.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Extended TiberiumClass class.
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
#include "tiberiumext.h"
#include "tiberium.h"
#include "ccini.h"
#include "extension.h"
#include "asserthandler.h"
#include "debughandler.h"
#include "overlaytype.h"
#include "tibsun_globals.h"
#include "vinifera_saveload.h"


/**
 *  Class constructor.
 *  
 *  @author: CCHyper
 */
TiberiumClassExtension::TiberiumClassExtension(const TiberiumClass *this_ptr) :
    AbstractTypeClassExtension(this_ptr)
{
    //if (this_ptr) EXT_DEBUG_TRACE("TiberiumClassExtension::TiberiumClassExtension - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    if (this_ptr)
    {
        /**
         *  By default Tiberium 0 gets green pips, and the rest get blue.
         *  Blue Tiberium is also drawn first
         */
        if (this_ptr->Get_Heap_ID() == 0)
        {
            PipIndex = 1;
            PipDrawOrder = 1;
        }
        else
        {
            PipIndex = 5;
            PipDrawOrder = 0;
        }
    }

    TiberiumExtensions.Add(this);
}


/**
 *  Class no-init constructor.
 *  
 *  @author: CCHyper
 */
TiberiumClassExtension::TiberiumClassExtension(const NoInitClass &noinit) :
    AbstractTypeClassExtension(noinit)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::TiberiumClassExtension(NoInitClass) - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));
}


/**
 *  Class destructor.
 *  
 *  @author: CCHyper
 */
TiberiumClassExtension::~TiberiumClassExtension()
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::~TiberiumClassExtension - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    TiberiumExtensions.Delete(this);
}


/**
 *  Retrieves the class identifier (CLSID) of the object.
 *  
 *  @author: CCHyper
 */
HRESULT TiberiumClassExtension::GetClassID(CLSID *lpClassID)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::GetClassID - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    if (lpClassID == nullptr) {
        return E_POINTER;
    }

    *lpClassID = __uuidof(this);

    return S_OK;
}


/**
 *  Initializes an object from the stream where it was saved previously.
 *  
 *  @author: CCHyper
 */
HRESULT TiberiumClassExtension::Load(IStream *pStm)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Load - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    HRESULT hr = AbstractTypeClassExtension::Load(pStm);
    if (FAILED(hr)) {
        return E_FAIL;
    }

    new (this) TiberiumClassExtension(NoInitClass());

    VINIFERA_SWIZZLE_REQUEST_POINTER_REMAP(Overlay, "Overlay");
    
    return hr;
}


/**
 *  Saves an object to the specified stream.
 *  
 *  @author: CCHyper
 */
HRESULT TiberiumClassExtension::Save(IStream *pStm, BOOL fClearDirty)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Save - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    HRESULT hr = AbstractTypeClassExtension::Save(pStm, fClearDirty);
    if (FAILED(hr)) {
        return hr;
    }

    return hr;
}


/**
 *  Return the raw size of class data for save/load purposes.
 *  
 *  @author: CCHyper
 */
int TiberiumClassExtension::Size_Of() const
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Size_Of - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    return sizeof(*this);
}


/**
 *  Removes the specified target from any targeting and reference trackers.
 *  
 *  @author: CCHyper
 */
void TiberiumClassExtension::Detach(TARGET target, bool all)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Detach - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));
}


/**
 *  Compute a unique crc value for this instance.
 *  
 *  @author: CCHyper
 */
void TiberiumClassExtension::Compute_CRC(WWCRCEngine &crc) const
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Compute_CRC - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));
}


/**
 *  Fetches the extension data from the INI database.  
 *  
 *  @author: CCHyper
 */
bool TiberiumClassExtension::Read_INI(CCINIClass &ini)
{
    //EXT_DEBUG_TRACE("TiberiumClassExtension::Read_INI - Name: %s (0x%08X)\n", Name(), (uintptr_t)(This()));

    // Default values set from the TiberiumType
    Overlay = This()->Image;
    UseSlopes = This()->NumSlopeFacings > 0;
    DamageToInfantry = This()->Power / 10;

    if (!AbstractTypeClassExtension::Read_INI(ini)) {
        return false;
    }

    const char *ini_name = Name();

    if (!ini.Is_Present(ini_name)) {
        return false;
    }

    PipIndex = ini.Get_Int(ini_name, "PipIndex", PipIndex);
    PipDrawOrder = ini.Get_Int(ini_name, "PipDrawOrder", PipDrawOrder);

    Overlay = (OverlayTypeClass*)ini.Get_Overlay(ini_name, "Overlay", Overlay);
    UseSlopes = ini.Get_Bool(ini_name, "UseSlopes", UseSlopes);

    This()->Image = Overlay;
    This()->NumFrames = 12;
    This()->NumImages = 12;
    This()->NumSlopeFacings = UseSlopes ? 8 : 0;

    DamageToInfantry = ini.Get_Int(ini_name, "DamageToInfantry", DamageToInfantry);
    
    return true;
}
