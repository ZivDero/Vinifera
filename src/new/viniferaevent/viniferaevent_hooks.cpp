/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          VINIFERAEVENT_HOOKS.H
 *
 *  @author        ZivDero
 *
 *  @brief         Contains the hooks for the Vinifera event class.
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

#include "viniferaevent_hooks.h"


 //DEFINE_HOOK(0x4C6CC8, Networking_RespondToEvent, 0x5)
 //{
 //	GET(EventExt*, pEvent, ESI);
 //	if (EventExt::IsValidType(pEvent->Type))
 //	{
 //		pEvent->RespondEvent();
 //	}
 //
 //	return 0;
 //}
 //
 //DEFINE_HOOK(0x64B6FE, sub_64B660_GetEventSize, 0x6)
 //{
 //	const auto eventType = static_cast<ViniferaEventType>(R->EDI() & 0xFF);
 //
 //	if (EventExt::IsValidType(eventType))
 //	{
 //		const size_t eventSize = EventExt::GetDataSize(eventType);
 //
 //		R->EDX(eventSize);
 //		R->EBP(eventSize);
 //		return 0x64B71D;
 //	}
 //
 //	return 0;
 //}
 //
 //DEFINE_HOOK(0x64BE7D, sub_64BDD0_GetEventSize1, 0x6)
 //{
 //	const auto eventType = static_cast<ViniferaEventType>(R->EDI() & 0xFF);
 //
 //	if (EventExt::IsValidType(eventType))
 //	{
 //		const size_t eventSize = EventExt::GetDataSize(eventType);
 //
 //		REF_STACK(size_t, eventSizeInStack, STACK_OFFSET(0xAC, -0x8C));
 //		eventSizeInStack = eventSize;
 //		R->ECX(eventSize);
 //		R->EBP(eventSize);
 //		return 0x64BE97;
 //	}
 //
 //	return 0;
 //}
 //
 //DEFINE_HOOK(0x64C30E, sub_64BDD0_GetEventSize2, 0x6)
 //{
 //	const auto eventType = static_cast<ViniferaEventType>(R->ESI() & 0xFF);
 //
 //	if (EventExt::IsValidType(eventType))
 //	{
 //		const size_t eventSize = EventExt::GetDataSize(eventType);
 //
 //		R->ECX(eventSize);
 //		R->EBP(eventSize);
 //		return 0x64C321;
 //	}
 //
 //	return 0;
 //}


void ViniferaEvent_Hooks()
{
    
}