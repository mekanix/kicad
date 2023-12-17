/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2012 Jean-Pierre Charras, jean-pierre.charras@ujf-grenoble.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2016 CERN
 * Copyright (C) 2012-2023 KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <functional>
using namespace std::placeholders;
#include <macros.h>
#include <pcb_edit_frame.h>
#include <pcb_track.h>
#include <pcb_group.h>
#include <pcb_generator.h>
#include <pcb_target.h>
#include <footprint.h>
#include <pad.h>
#include <origin_viewitem.h>
#include <connectivity/connectivity_data.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tools/pcb_selection_tool.h>
#include <tools/pcb_control.h>
#include <tools/board_editor_control.h>
#include <drawing_sheet/ds_proxy_undo_item.h>
#include <wx/msgdlg.h>

/* Functions to undo and redo edit commands.
 *  commands to undo are stored in CurrentScreen->m_UndoList
 *  commands to redo are stored in CurrentScreen->m_RedoList
 *
 *  m_UndoList and m_RedoList handle a std::vector of PICKED_ITEMS_LIST
 *  Each PICKED_ITEMS_LIST handle a std::vector of pickers (class ITEM_PICKER),
 *  that store the list of schematic items that are concerned by the command to undo or redo
 *  and is created for each command to undo (handle also a command to redo).
 *  each picker has a pointer pointing to an item to undo or redo (in fact: deleted, added or
 *  modified),
 * and has a pointer to a copy of this item, when this item has been modified
 * (the old values of parameters are therefore saved)
 *
 *  there are 3 cases:
 *  - delete item(s) command
 *  - change item(s) command
 *  - add item(s) command
 *
 *  Undo command
 *  - delete item(s) command:
 *       =>  deleted items are moved in undo list
 *
 *  - change item(s) command
 *      => A copy of item(s) is made (a DrawPickedStruct list of wrappers)
 *      the .m_Link member of each wrapper points the modified item.
 *      the .m_Item member of each wrapper points the old copy of this item.
 *
 *  - add item(s) command
 *      =>A list of item(s) is made. The .m_Item member of each wrapper points the new item.
 *
 *  Redo command
 *  - delete item(s) old command:
 *      => deleted items are moved in EEDrawList list, and in
 *
 *  - change item(s) command
 *      => the copy of item(s) is moved in Undo list
 *
 *  - add item(s) command
 *      => The list of item(s) is used to create a deleted list in undo list(same as a delete
 *         command)
 *
 *   Some block operations that change items can be undone without memorize items, just the
 *   coordinates of the transform:
 *      move list of items (undo/redo is made by moving with the opposite move vector)
 *      mirror (Y) and flip list of items (undo/redo is made by mirror or flip items)
 *      so they are handled specifically.
 *
 */


void PCB_BASE_EDIT_FRAME::saveCopyInUndoList( PICKED_ITEMS_LIST* commandToUndo,
                                              const PICKED_ITEMS_LIST& aItemsList,
                                              UNDO_REDO aCommandType )
{
    int preExisting = commandToUndo->GetCount();

    for( unsigned ii = 0; ii < aItemsList.GetCount(); ii++ )
        commandToUndo->PushItem( aItemsList.GetItemWrapper(ii) );

    for( unsigned ii = preExisting; ii < commandToUndo->GetCount(); ii++ )
    {
        EDA_ITEM* item    = commandToUndo->GetPickedItem( ii );
        UNDO_REDO command = commandToUndo->GetPickedItemStatus( ii );

        if( command == UNDO_REDO::UNSPECIFIED )
        {
            command = aCommandType;
            commandToUndo->SetPickedItemStatus( command, ii );
        }

        wxASSERT( item );

        switch( command )
        {
        case UNDO_REDO::CHANGED:
        case UNDO_REDO::DRILLORIGIN:
        case UNDO_REDO::GRIDORIGIN:
            // If we don't yet have a copy in the link, set one up
            if( !commandToUndo->GetPickedItemLink( ii ) )
            {
                // Warning: DRILLORIGIN and GRIDORIGIN undo/redo command create EDA_ITEMs
                // that cannot be casted blindly to BOARD_ITEMs (a BOARD_ITEM is derived from a EDA_ITEM)
                // Especially SetParentGroup() does not exist in EDA_ITEM
                EDA_ITEM* clone = static_cast<EDA_ITEM*>( item->Clone() );

                if( BOARD_ITEM* brdclone = dynamic_cast<BOARD_ITEM*>( clone ) )
                    brdclone->SetParentGroup( nullptr );

                commandToUndo->SetPickedItemLink( clone, ii );
            }

            break;

        case UNDO_REDO::NEWITEM:
        case UNDO_REDO::DELETED:
        case UNDO_REDO::PAGESETTINGS:
        case UNDO_REDO::REGROUP:
        case UNDO_REDO::UNGROUP:
            break;

        default:
            wxFAIL_MSG( wxString::Format( wxT( "Unrecognized undo command: %X" ), command ) );
            break;
        }
    }

    if( commandToUndo->GetCount() )
    {
        /* Save the copy in undo list */
        PushCommandToUndoList( commandToUndo );

        /* Clear redo list, because after a new command one cannot redo a command */
        ClearUndoORRedoList( REDO_LIST );
    }
    else
    {
        // Should not occur
        wxASSERT( false );
        delete commandToUndo;
    }
}


void PCB_BASE_EDIT_FRAME::SaveCopyInUndoList( EDA_ITEM* aItem, UNDO_REDO aCommandType )
{
    PICKED_ITEMS_LIST* commandToUndo = new PICKED_ITEMS_LIST();
    PICKED_ITEMS_LIST  itemsList;

    itemsList.PushItem( ITEM_PICKER( nullptr, aItem, aCommandType ) );
    saveCopyInUndoList( commandToUndo, itemsList, aCommandType );
}


void PCB_BASE_EDIT_FRAME::SaveCopyInUndoList( const PICKED_ITEMS_LIST& aItemsList,
                                              UNDO_REDO aCommandType )
{
    PICKED_ITEMS_LIST* commandToUndo = new PICKED_ITEMS_LIST();
    commandToUndo->SetDescription( aItemsList.GetDescription() );

    saveCopyInUndoList( commandToUndo, aItemsList, aCommandType );
}


void PCB_BASE_EDIT_FRAME::AppendCopyToUndoList( const PICKED_ITEMS_LIST& aItemsList,
                                                UNDO_REDO aCommandType )
{
    PICKED_ITEMS_LIST* commandToUndo = PopCommandFromUndoList();

    if( !commandToUndo )
    {
        commandToUndo = new PICKED_ITEMS_LIST();
        commandToUndo->SetDescription( aItemsList.GetDescription() );
    }

    saveCopyInUndoList( commandToUndo, aItemsList, aCommandType );
}


void PCB_BASE_EDIT_FRAME::RestoreCopyFromUndoList( wxCommandEvent& aEvent )
{
    if( UndoRedoBlocked() )
        return;

    if( GetUndoCommandCount() <= 0 )
        return;

    // Inform tools that undo command was issued
    m_toolManager->ProcessEvent( { TC_MESSAGE, TA_UNDO_REDO_PRE, AS_GLOBAL } );

    // Get the old list
    PICKED_ITEMS_LIST* list = PopCommandFromUndoList();

    // Undo the command
    PutDataInPreviousState( list );

    // Put the old list in RedoList
    list->ReversePickersListOrder();
    PushCommandToRedoList( list );

    OnModify();

    m_toolManager->ProcessEvent( { TC_MESSAGE, TA_UNDO_REDO_POST, AS_GLOBAL } );
    m_toolManager->PostEvent( EVENTS::SelectedItemsModified );

    GetCanvas()->Refresh();
}


void PCB_BASE_EDIT_FRAME::RestoreCopyFromRedoList( wxCommandEvent& aEvent )
{
    if( UndoRedoBlocked() )
        return;

    if( GetRedoCommandCount() == 0 )
        return;

    // Inform tools that redo command was issued
    m_toolManager->ProcessEvent( EVENTS::UndoRedoPreEvent );

    // Get the old list
    PICKED_ITEMS_LIST* list = PopCommandFromRedoList();

    // Redo the command
    PutDataInPreviousState( list );

    // Put the old list in UndoList
    list->ReversePickersListOrder();
    PushCommandToUndoList( list );

    OnModify();

    m_toolManager->ProcessEvent( EVENTS::UndoRedoPostEvent );
    m_toolManager->PostEvent( EVENTS::SelectedItemsModified );

    GetCanvas()->Refresh();
}


void PCB_BASE_EDIT_FRAME::PutDataInPreviousState( PICKED_ITEMS_LIST* aList )
{
    bool not_found = false;
    bool reBuild_ratsnest = false;
    bool deep_reBuild_ratsnest = false;  // true later if pointers must be rebuilt
    bool solder_mask_dirty = false;

    auto view = GetCanvas()->GetView();
    auto connectivity = GetBoard()->GetConnectivity();

    GetBoard()->IncrementTimeStamp();   // clear caches

    // Undo in the reverse order of list creation: (this can allow stacked changes
    // like the same item can be changes and deleted in the same complex command

    // Restore changes in reverse order
    for( int ii = (int) aList->GetCount() - 1; ii >= 0 ; ii-- )
    {
        EDA_ITEM* eda_item = aList->GetPickedItem( (unsigned) ii );

        /* Test for existence of item on board.
         * It could be deleted, and no more on board:
         *   - if a call to SaveCopyInUndoList was forgotten in Pcbnew
         *   - in zones outlines, when a change in one zone merges this zone with an other
         * This test avoids a Pcbnew crash
         * Obviously, this test is not made for deleted items
         */
        UNDO_REDO status = aList->GetPickedItemStatus( ii );

        if( status != UNDO_REDO::DELETED
                && status != UNDO_REDO::REGROUP
                && status != UNDO_REDO::UNGROUP
                && status != UNDO_REDO::DRILLORIGIN     // origin markers never on board
                && status != UNDO_REDO::GRIDORIGIN      // origin markers never on board
                && status != UNDO_REDO::PAGESETTINGS )  // nor are page settings proxy items
        {
            if( GetBoard()->GetItem( eda_item->m_Uuid ) == DELETED_BOARD_ITEM::GetInstance() )
            {
                // Checking if it ever happens
                wxASSERT_MSG( false, wxT( "Item in the undo buffer does not exist" ) );

                // Remove this non existent item
                aList->RemovePicker( ii );
                not_found = true;

                if( aList->GetCount() == 0 )
                    break;

                continue;
            }
        }

        // see if we must rebuild ratsnets and pointers lists
        switch( eda_item->Type() )
        {
        case PCB_FOOTPRINT_T:
            deep_reBuild_ratsnest = true;   // Pointers on pads can be invalid
            KI_FALLTHROUGH;

        case PCB_ZONE_T:
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
        case PCB_PAD_T:
            reBuild_ratsnest = true;
            break;

        case PCB_NETINFO_T:
            reBuild_ratsnest = true;
            deep_reBuild_ratsnest = true;
            break;

        default:
            break;
        }

        switch( eda_item->Type() )
        {
        case PCB_FOOTPRINT_T:
            solder_mask_dirty = true;
            break;

        case PCB_VIA_T:
            solder_mask_dirty = true;
            break;

        case PCB_ZONE_T:
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_PAD_T:
        case PCB_SHAPE_T:
        {
            LSET layers = static_cast<BOARD_ITEM*>( eda_item )->GetLayerSet();

            if( layers.test( F_Mask ) || layers.test( B_Mask ) )
                solder_mask_dirty = true;

            break;
        }

        default:
            break;
        }

        switch( aList->GetPickedItemStatus( ii ) )
        {
        case UNDO_REDO::CHANGED:    /* Exchange old and new data for each item */
        {
            BOARD_ITEM* item = (BOARD_ITEM*) eda_item;
            BOARD_ITEM* image = (BOARD_ITEM*) aList->GetPickedItemLink( ii );

            // Remove all pads/drawings/texts, as they become invalid
            // for the VIEW after SwapItemData() called for footprints
            view->Remove( item );
            connectivity->Remove( item );

            item->SwapItemData( image );

            if( PCB_GROUP* group = dynamic_cast<PCB_GROUP*>( item ) )
            {
                group->RunOnChildren( [&]( BOARD_ITEM* child )
                                      {
                                          child->SetParentGroup( group );
                                      } );
            }

            view->Add( item );
            view->Hide( item, false );
            connectivity->Add( item );
            item->GetBoard()->OnItemChanged( item );
            break;
        }

        case UNDO_REDO::NEWITEM:        /* new items are deleted */
            aList->SetPickedItemStatus( UNDO_REDO::DELETED, ii );
            GetModel()->Remove( (BOARD_ITEM*) eda_item );

            if( eda_item->Type() != PCB_NETINFO_T )
                view->Remove( eda_item );

            break;

        case UNDO_REDO::DELETED:    /* deleted items are put in List, as new items */
            aList->SetPickedItemStatus( UNDO_REDO::NEWITEM, ii );
            GetModel()->Add( (BOARD_ITEM*) eda_item );

            if( eda_item->Type() != PCB_NETINFO_T )
                view->Add( eda_item );

            break;

        case UNDO_REDO::REGROUP:    /* grouped items are ungrouped */
            aList->SetPickedItemStatus( UNDO_REDO::UNGROUP, ii );

            if( BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( eda_item ) )
            {
                if( PCB_GROUP* group = boardItem->GetParentGroup() )
                {
                    if( !aList->GetPickedItemLink( ii ) )
                        aList->SetPickedItemLink( group->Clone(), ii );

                    group->RemoveItem( boardItem );
                }
            }

            break;

        case UNDO_REDO::UNGROUP:    /* ungrouped items are re-added to their previuos groups */
            aList->SetPickedItemStatus( UNDO_REDO::REGROUP, ii );

            if( BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( eda_item ) )
            {
                PCB_GROUP* group = nullptr;

                // The link is just a clone of the original parent group; we need to look up
                // the UUID in the document to find the real parent.
                if( EDA_ITEM* link = aList->GetPickedItemLink( ii ) )
                    group = dynamic_cast<PCB_GROUP*>( GetBoard()->GetItem( link->m_Uuid ) );

                if( group )
                    group->AddItem( boardItem );
            }

            break;

        case UNDO_REDO::DRILLORIGIN:
        case UNDO_REDO::GRIDORIGIN:
        {
            // Warning: DRILLORIGIN and GRIDORIGIN undo/redo command create EDA_ITEMs
            // that cannot be casted to BOARD_ITEMs
            EDA_ITEM* image = aList->GetPickedItemLink( ii );
            VECTOR2D origin = image->GetPosition();
            image->SetPosition( eda_item->GetPosition() );

            if( aList->GetPickedItemStatus( ii ) == UNDO_REDO::DRILLORIGIN )
                BOARD_EDITOR_CONTROL::DoSetDrillOrigin( view, this, eda_item, origin );
            else
                PCB_CONTROL::DoSetGridOrigin( view, this, eda_item, origin );

            break;
        }

        case UNDO_REDO::PAGESETTINGS:
        {
            // swap current settings with stored settings
            DS_PROXY_UNDO_ITEM  alt_item( this );
            DS_PROXY_UNDO_ITEM* item = static_cast<DS_PROXY_UNDO_ITEM*>( eda_item );
            item->Restore( this );
            *item = alt_item;
            break;
        }

        default:
            wxFAIL_MSG( wxString::Format( wxT( "PutDataInPreviousState() error (unknown code %X)" ),
                                          aList->GetPickedItemStatus( ii ) ) );
            break;
        }
    }

    if( not_found )
        wxMessageBox( _( "Incomplete undo/redo operation: some items not found" ) );

    if( IsType( FRAME_PCB_EDITOR ) )
    {
        if( reBuild_ratsnest || deep_reBuild_ratsnest )
            Compile_Ratsnest( false );

        if( solder_mask_dirty )
            HideSolderMask();
    }

    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();
    selTool->RebuildSelection();

    GetBoard()->SanitizeNetcodes();
}


void PCB_BASE_EDIT_FRAME::ClearUndoORRedoList( UNDO_REDO_LIST whichList, int aItemCount )
{
    if( aItemCount == 0 )
        return;

    UNDO_REDO_CONTAINER& list = whichList == UNDO_LIST ? m_undoList : m_redoList;
    unsigned             icnt = list.m_CommandsList.size();

    if( aItemCount > 0 )
        icnt = aItemCount;

    for( unsigned ii = 0; ii < icnt; ii++ )
    {
        if( list.m_CommandsList.size() == 0 )
            break;

        PICKED_ITEMS_LIST* curr_cmd = list.m_CommandsList[0];
        list.m_CommandsList.erase( list.m_CommandsList.begin() );
        ClearListAndDeleteItems( curr_cmd );
        delete curr_cmd;    // Delete command
    }
}


void PCB_BASE_EDIT_FRAME::ClearListAndDeleteItems( PICKED_ITEMS_LIST* aList )
{
    aList->ClearListAndDeleteItems(
            []( EDA_ITEM* item )
            {
                if( BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( item ) )
                    boardItem->SetParentGroup( nullptr );

                delete item;
            } );
}


void PCB_BASE_EDIT_FRAME::RollbackFromUndo()
{
    PICKED_ITEMS_LIST* undo = PopCommandFromUndoList();
    PutDataInPreviousState( undo );
    ClearListAndDeleteItems( undo );
    delete undo;

    GetCanvas()->Refresh();
}
