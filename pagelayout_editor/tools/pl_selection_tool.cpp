/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019 CERN
 * Copyright (C) 2019 KiCad Developers, see AUTHORS.txt for contributors.
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


#include <bitmaps.h>
#include <view/view.h>
#include <view/view_controls.h>
#include <preview_items/selection_area.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <tool/selection.h>
#include <page_layout/ws_data_item.h>
#include <page_layout/ws_data_model.h>
#include <page_layout/ws_draw_item.h>
#include <collector.h>
#include <math/util.h>      // for KiROUND

#include "pl_editor_frame.h"
#include "pl_selection_tool.h"
#include "tools/pl_actions.h"

/**
 * The maximum number of items in the clarify selection context menu.  The current
 * setting of 40 is arbitrary.
 */
#define MAX_SELECT_ITEM_IDS 40


SELECTION_CONDITION PL_CONDITIONS::Idle = [] (const SELECTION& aSelection )
{
    return ( !aSelection.Front() || aSelection.Front()->GetEditFlags() == 0 );
};


#define HITTEST_THRESHOLD_PIXELS 3


PL_SELECTION_TOOL::PL_SELECTION_TOOL() :
        TOOL_INTERACTIVE( "plEditor.InteractiveSelection" ),
        m_frame( nullptr ),
        m_additive( false ),
        m_subtractive( false ),
        m_exclusive_or( false ),
        m_multiple( false ),
        m_skip_heuristics( false )
{
}


bool PL_SELECTION_TOOL::Init()
{
    m_frame = getEditFrame<PL_EDITOR_FRAME>();

    auto& menu = m_menu.GetMenu();

    menu.AddSeparator( 200 );
    menu.AddItem( PL_ACTIONS::drawLine,                PL_CONDITIONS::Idle, 250 );
    menu.AddItem( PL_ACTIONS::drawRectangle,           PL_CONDITIONS::Idle, 250 );
    menu.AddItem( PL_ACTIONS::placeText,               PL_CONDITIONS::Idle, 250 );
    menu.AddItem( PL_ACTIONS::placeImage,              PL_CONDITIONS::Idle, 250 );
    menu.AddItem( PL_ACTIONS::appendImportedWorksheet, PL_CONDITIONS::Idle, 250 );

    menu.AddSeparator( 1000 );
    m_frame->AddStandardSubMenus( m_menu );

    return true;
}


void PL_SELECTION_TOOL::Reset( RESET_REASON aReason )
{
    if( aReason == MODEL_RELOAD )
        m_frame = getEditFrame<PL_EDITOR_FRAME>();
}


int PL_SELECTION_TOOL::UpdateMenu( const TOOL_EVENT& aEvent )
{
    ACTION_MENU*      actionMenu = aEvent.Parameter<ACTION_MENU*>();
    CONDITIONAL_MENU* conditionalMenu = dynamic_cast<CONDITIONAL_MENU*>( actionMenu );

    if( conditionalMenu )
        conditionalMenu->Evaluate( m_selection );

    if( actionMenu )
        actionMenu->UpdateAll();

    return 0;
}


int PL_SELECTION_TOOL::Main( const TOOL_EVENT& aEvent )
{
    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        m_additive = m_subtractive = m_exclusive_or = false;

        if( evt->Modifier( MD_SHIFT ) && evt->Modifier( MD_CTRL ) )
            m_subtractive = true;
        else if( evt->Modifier( MD_SHIFT ) )
            m_additive = true;
        else if( evt->Modifier( MD_CTRL ) )
            m_exclusive_or = true;

        bool modifier_enabled = m_subtractive || m_additive || m_exclusive_or;

        // Is the user requesting that the selection list include all possible
        // items without removing less likely selection candidates
        m_skip_heuristics = !!evt->Modifier( MD_ALT );

        // Single click? Select single object
        if( evt->IsClick( BUT_LEFT ) )
        {
            SelectPoint( evt->Position() );
        }

        // right click? if there is any object - show the context menu
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            bool selectionCancelled = false;

            if( m_selection.Empty() )
            {
                SelectPoint( evt->Position(), &selectionCancelled );
                m_selection.SetIsHover( true );
            }

            if( !selectionCancelled )
                m_menu.ShowContextMenu( m_selection );
        }

        // double click? Display the properties window
        else if( evt->IsDblClick( BUT_LEFT ) )
        {
            // No double-click actions currently defined
        }

        // drag with LMB? Select multiple objects (or at least draw a selection box) or drag them
        else if( evt->IsDrag( BUT_LEFT ) )
        {
            if( modifier_enabled || m_selection.Empty() )
            {
                selectMultiple();
            }
            else
            {
                // Check if dragging has started within any of selected items bounding box
                if( selectionContains( evt->Position() ) )
                {
                    // Yes -> run the move tool and wait till it finishes
                    m_toolMgr->RunAction( "plEditor.InteractiveMove.move", true );
                }
                else
                {
                    // No -> clear the selection list
                    ClearSelection();
                }
            }
        }

        // Middle double click?  Do zoom to fit or zoom to objects
        else if( evt->IsDblClick( BUT_MIDDLE ) )
        {
            m_toolMgr->RunAction( ACTIONS::zoomFitScreen, true );
        }

        else if( evt->IsCancelInteractive() )
        {
            ClearSelection();
        }

        else if( evt->Action() == TA_UNDO_REDO_PRE )
        {
            ClearSelection();
        }

        else
            evt->SetPassEvent();


        if( m_frame->ToolStackIsEmpty() )
        {
            if( !modifier_enabled
                    && !m_selection.Empty()
                    && m_frame->GetDragAction() == KIGFX::MOUSE_DRAG_ACTION::DRAG_SELECTED
                    && evt->HasPosition()
                    && selectionContains( evt->Position() ) )
            {
                m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::MOVING );
            }
            else
            {
                if( m_additive )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ADD );
                else if( m_subtractive )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::SUBTRACT );
                else if( m_exclusive_or )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::XOR );
                else
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
            }
        }
    }

    return 0;
}


PL_SELECTION& PL_SELECTION_TOOL::GetSelection()
{
    return m_selection;
}


void PL_SELECTION_TOOL::SelectPoint( const VECTOR2I& aWhere, bool* aSelectionCancelledFlag )
{
    int threshold = KiROUND( getView()->ToWorld( HITTEST_THRESHOLD_PIXELS ) );

    // locate items.
    COLLECTOR collector;

    for( WS_DATA_ITEM* dataItem : WS_DATA_MODEL::GetTheInstance().GetItems() )
    {
        for( WS_DRAW_ITEM_BASE* drawItem : dataItem->GetDrawItems() )
        {
            if( drawItem->HitTest( (wxPoint) aWhere, threshold ) )
                collector.Append( drawItem );
        }
    }

    m_selection.ClearReferencePoint();

    // Apply some ugly heuristics to avoid disambiguation menus whenever possible
    if( collector.GetCount() > 1 && !m_skip_heuristics )
    {
        guessSelectionCandidates( collector, aWhere );
    }

    // If still more than one item we're going to have to ask the user.
    if( collector.GetCount() > 1 )
    {
        // Must call selectionMenu via RunAction() to avoid event-loop contention
        m_toolMgr->RunAction( PL_ACTIONS::selectionMenu, true, &collector );

        if( collector.m_MenuCancelled )
        {
            if( aSelectionCancelledFlag )
                *aSelectionCancelledFlag = true;

            return;
        }
    }

    if( !m_additive && !m_subtractive && !m_exclusive_or )
        ClearSelection();

    bool anyAdded      = false;
    bool anySubtracted = false;

    if( collector.GetCount() > 0 )
    {
        for( int i = 0; i < collector.GetCount(); ++i )
        {
            if( m_subtractive || ( m_exclusive_or && collector[i]->IsSelected() ) )
            {
                unselect( collector[i] );
                anySubtracted = true;
            }
            else
            {
                select( collector[i] );
                anyAdded = true;
            }
        }
    }

    if( anyAdded )
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    if( anySubtracted )
        m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
}


void PL_SELECTION_TOOL::guessSelectionCandidates( COLLECTOR& collector, const VECTOR2I& aPos )
{
    // There are certain conditions that can be handled automatically.

    // Prefer an exact hit to a sloppy one
    for( int i = 0; collector.GetCount() == 2 && i < 2; ++i )
    {
        EDA_ITEM* item = collector[ i ];
        EDA_ITEM* other = collector[ ( i + 1 ) % 2 ];

        if( item->HitTest( (wxPoint) aPos, 0 ) && !other->HitTest( (wxPoint) aPos, 0 ) )
            collector.Transfer( other );
    }
}


PL_SELECTION& PL_SELECTION_TOOL::RequestSelection()
{
    // If nothing is selected do a hover selection
    if( m_selection.Empty() )
    {
        VECTOR2D cursorPos = getViewControls()->GetCursorPosition( true );

        ClearSelection();
        SelectPoint( cursorPos );
        m_selection.SetIsHover( true );
    }

    return m_selection;
}


bool PL_SELECTION_TOOL::selectMultiple()
{
    bool cancelled = false;     // Was the tool cancelled while it was running?
    m_multiple = true;          // Multiple selection mode is active
    KIGFX::VIEW* view = getView();

    KIGFX::PREVIEW::SELECTION_AREA area;
    view->Add( &area );

    while( TOOL_EVENT* evt = Wait() )
    {
        int width = area.GetEnd().x - area.GetOrigin().x;

        /* Selection mode depends on direction of drag-selection:
         * Left > Right : Select objects that are fully enclosed by selection
         * Right > Left : Select objects that are crossed by selection
         */
        bool windowSelection = width >= 0 ? true : false;

        m_frame->GetCanvas()->SetCurrentCursor(
                windowSelection ? KICURSOR::SELECT_WINDOW : KICURSOR::SELECT_LASSO );

        if( evt->IsCancelInteractive() || evt->IsActivate() )
        {
            cancelled = true;
            break;
        }

        if( evt->IsDrag( BUT_LEFT ) )
        {
            if( !m_additive && !m_subtractive && !m_exclusive_or )
                ClearSelection();

            // Start drawing a selection box
            area.SetOrigin( evt->DragOrigin() );
            area.SetEnd( evt->Position() );
            area.SetAdditive( m_additive );
            area.SetSubtractive( m_subtractive );
            area.SetExclusiveOr( m_exclusive_or );

            view->SetVisible( &area, true );
            view->Update( &area );
            getViewControls()->SetAutoPan( true );
        }

        if( evt->IsMouseUp( BUT_LEFT ) )
        {
            getViewControls()->SetAutoPan( false );

            // End drawing the selection box
            view->SetVisible( &area, false );

            int height = area.GetEnd().y - area.GetOrigin().y;

            bool anyAdded = false;
            bool anySubtracted = false;

            // Construct an EDA_RECT to determine EDA_ITEM selection
            EDA_RECT selectionRect( (wxPoint)area.GetOrigin(), wxSize( width, height ) );

            selectionRect.Normalize();

            for( WS_DATA_ITEM* dataItem : WS_DATA_MODEL::GetTheInstance().GetItems() )
            {
                for( WS_DRAW_ITEM_BASE* item : dataItem->GetDrawItems() )
                {
                    if( item->HitTest( selectionRect, windowSelection ) )
                    {
                        if( m_subtractive || ( m_exclusive_or && item->IsSelected() ) )
                        {
                            unselect( item );
                            anySubtracted = true;
                        }
                        else
                        {
                            select( item );
                            anyAdded = true;
                        }
                    }
                }
            }

            // Inform other potentially interested tools
            if( anyAdded )
                m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

            if( anySubtracted )
                m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );

            break;  // Stop waiting for events
        }
    }

    getViewControls()->SetAutoPan( false );

    // Stop drawing the selection box
    view->Remove( &area );
    m_multiple = false;         // Multiple selection mode is inactive

    if( !cancelled )
        m_selection.ClearReferencePoint();

    return cancelled;
}


int PL_SELECTION_TOOL::AddItemToSel( const TOOL_EVENT& aEvent )
{
    AddItemToSel( aEvent.Parameter<EDA_ITEM*>() );
    return 0;
}


void PL_SELECTION_TOOL::AddItemToSel( EDA_ITEM* aItem, bool aQuietMode )
{
    if( aItem )
    {
        select( aItem );

        // Inform other potentially interested tools
        if( !aQuietMode )
            m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
    }
}


int PL_SELECTION_TOOL::AddItemsToSel( const TOOL_EVENT& aEvent )
{
    AddItemsToSel( aEvent.Parameter<EDA_ITEMS*>(), false );
    return 0;
}


void PL_SELECTION_TOOL::AddItemsToSel( EDA_ITEMS* aList, bool aQuietMode )
{
    if( aList )
    {
        for( EDA_ITEM* item : *aList )
            select( item );

        // Inform other potentially interested tools
        if( !aQuietMode )
            m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
    }
}


int PL_SELECTION_TOOL::RemoveItemFromSel( const TOOL_EVENT& aEvent )
{
    RemoveItemFromSel( aEvent.Parameter<EDA_ITEM*>() );
    return 0;
}


void PL_SELECTION_TOOL::RemoveItemFromSel( EDA_ITEM* aItem, bool aQuietMode )
{
    if( aItem )
    {
        unselect( aItem );

        // Inform other potentially interested tools
        if( !aQuietMode )
            m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
    }
}


int PL_SELECTION_TOOL::RemoveItemsFromSel( const TOOL_EVENT& aEvent )
{
    RemoveItemsFromSel( aEvent.Parameter<EDA_ITEMS*>(), false );
    return 0;
}


void PL_SELECTION_TOOL::RemoveItemsFromSel( EDA_ITEMS* aList, bool aQuietMode )
{
    if( aList )
    {
        for( EDA_ITEM* item : *aList )
            unselect( item );

        // Inform other potentially interested tools
        if( !aQuietMode )
            m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
    }
}


void PL_SELECTION_TOOL::BrightenItem( EDA_ITEM* aItem )
{
    highlight( aItem, BRIGHTENED );
}


void PL_SELECTION_TOOL::UnbrightenItem( EDA_ITEM* aItem )
{
    unhighlight( aItem, BRIGHTENED );
}


int PL_SELECTION_TOOL::ClearSelection( const TOOL_EVENT& aEvent )
{
    ClearSelection();
    return 0;
}


void PL_SELECTION_TOOL::RebuildSelection()
{
    m_selection.Clear();

    for( WS_DATA_ITEM* dataItem : WS_DATA_MODEL::GetTheInstance().GetItems() )
    {
        for( WS_DRAW_ITEM_BASE* item : dataItem->GetDrawItems() )
        {
            if( item->IsSelected() )
                select( item );
        }
    }
}


int PL_SELECTION_TOOL::SelectionMenu( const TOOL_EVENT& aEvent )
{
    COLLECTOR* collector = aEvent.Parameter<COLLECTOR*>();

    if( !doSelectionMenu( collector ) )
        collector->m_MenuCancelled = true;

    return 0;
}


bool PL_SELECTION_TOOL::doSelectionMenu( COLLECTOR* aCollector )
{
    EDA_ITEM*   current = nullptr;
    ACTION_MENU menu( true );

    // ID limit is `MAX_SELECT_ITEM_IDS+1` because the last item is "select all"
    // and the first item has ID of 1.
    int limit = std::min( MAX_SELECT_ITEM_IDS + 1, aCollector->GetCount() );

    for( int i = 0; i < limit; ++i )
    {
        wxString text;
        EDA_ITEM* item = ( *aCollector )[i];
        text = item->GetSelectMenuText( m_frame->GetUserUnits() );

        wxString menuText = wxString::Format( "&%d. %s\t%d", i + 1, text, i + 1 );
        menu.Add( menuText, i + 1, item->GetMenuImage() );
    }

    menu.AppendSeparator();
    menu.Add( _( "Select &All\tA" ), limit + 1, plus_xpm );

    if( aCollector->m_MenuTitle.Length() )
    {
        menu.SetTitle( aCollector->m_MenuTitle );
        menu.SetIcon( info_xpm );
        menu.DisplayTitle( true );
    }
    else
    {
        menu.DisplayTitle( false );
    }

    SetContextMenu( &menu, CMENU_NOW );

    bool selectAll = false;

    while( TOOL_EVENT* evt = Wait() )
    {
        if( evt->Action() == TA_CHOICE_MENU_UPDATE )
        {
            if( selectAll )
            {
                for( int i = 0; i < aCollector->GetCount(); ++i )
                    unhighlight( ( *aCollector )[i], BRIGHTENED );
            }
            else if( current )
            {
                unhighlight( current, BRIGHTENED );
            }

            int id = *evt->GetCommandId();

            // User has pointed an item, so show it in a different way
            if( id > 0 && id <= limit )
            {
                current = ( *aCollector )[id - 1];
                highlight( current, BRIGHTENED );
            }
            else
            {
                current = nullptr;
            }

            if( id == limit + 1 )
            {
                for( int i = 0; i < aCollector->GetCount(); ++i )
                    highlight( ( *aCollector )[i], BRIGHTENED );

                selectAll = true;
            }
            else
            {
                selectAll = false;
            }
        }
        else if( evt->Action() == TA_CHOICE_MENU_CHOICE )
        {
            if( selectAll )
            {
                for( int i = 0; i < aCollector->GetCount(); ++i )
                    unhighlight( ( *aCollector )[i], BRIGHTENED );
            }
            else if( current )
            {
                unhighlight( current, BRIGHTENED );
            }

            OPT<int> id = evt->GetCommandId();

            // User has selected an item, so this one will be returned
            if( id == limit + 1 )
            {
                selectAll = true;
                current   = nullptr;
            }
            else if( id && ( *id > 0 ) && ( *id <= limit ) )
            {
                selectAll = false;
                current = ( *aCollector )[*id - 1];
            }
            else
            {
                selectAll = false;
                current   = nullptr;
            }
        }
        else if( evt->Action() == TA_CHOICE_MENU_CLOSED )
        {
            break;
        }

        getView()->UpdateItems();
        m_frame->GetCanvas()->Refresh();
    }

    if( selectAll )
    {
        return true;
    }
    else if( current )
    {
        unhighlight( current, BRIGHTENED );

        getView()->UpdateItems();
        m_frame->GetCanvas()->Refresh();

        aCollector->Empty();
        aCollector->Append( current );
        return true;
    }

    return false;
}


void PL_SELECTION_TOOL::ClearSelection()
{
    if( m_selection.Empty() )
        return;

    while( m_selection.GetSize() )
        unhighlight( (EDA_ITEM*) m_selection.Front(), SELECTED, &m_selection );

    getView()->Update( &m_selection );

    m_selection.SetIsHover( false );
    m_selection.ClearReferencePoint();

    // Inform other potentially interested tools
    m_toolMgr->ProcessEvent( EVENTS::ClearedEvent );
}


void PL_SELECTION_TOOL::select( EDA_ITEM* aItem )
{
    highlight( aItem, SELECTED, &m_selection );
}


void PL_SELECTION_TOOL::unselect( EDA_ITEM* aItem )
{
    unhighlight( aItem, SELECTED, &m_selection );
}


void PL_SELECTION_TOOL::highlight( EDA_ITEM* aItem, int aMode, PL_SELECTION* aGroup )
{
    if( aMode == SELECTED )
        aItem->SetSelected();
    else if( aMode == BRIGHTENED )
        aItem->SetBrightened();

    if( aGroup )
        aGroup->Add( aItem );

    getView()->Update( aItem );
}


void PL_SELECTION_TOOL::unhighlight( EDA_ITEM* aItem, int aMode, PL_SELECTION* aGroup )
{
    if( aMode == SELECTED )
        aItem->ClearSelected();
    else if( aMode == BRIGHTENED )
        aItem->ClearBrightened();

    if( aGroup )
        aGroup->Remove( aItem );

    getView()->Update( aItem );
}


bool PL_SELECTION_TOOL::selectionContains( const VECTOR2I& aPoint ) const
{
    const unsigned GRIP_MARGIN = 20;
    VECTOR2I margin = getView()->ToWorld( VECTOR2I( GRIP_MARGIN, GRIP_MARGIN ), false );

    // Check if the point is located within any of the currently selected items bounding boxes
    for( auto item : m_selection )
    {
        BOX2I itemBox = item->ViewBBox();
        itemBox.Inflate( margin.x, margin.y );    // Give some margin for gripping an item

        if( itemBox.Contains( aPoint ) )
            return true;
    }

    return false;
}


void PL_SELECTION_TOOL::setTransitions()
{
    Go( &PL_SELECTION_TOOL::UpdateMenu,            ACTIONS::updateMenu.MakeEvent() );

    Go( &PL_SELECTION_TOOL::Main,                  PL_ACTIONS::selectionActivate.MakeEvent() );
    Go( &PL_SELECTION_TOOL::ClearSelection,        PL_ACTIONS::clearSelection.MakeEvent() );

    Go( &PL_SELECTION_TOOL::AddItemToSel,          PL_ACTIONS::addItemToSel.MakeEvent() );
    Go( &PL_SELECTION_TOOL::AddItemsToSel,         PL_ACTIONS::addItemsToSel.MakeEvent() );
    Go( &PL_SELECTION_TOOL::RemoveItemFromSel,     PL_ACTIONS::removeItemFromSel.MakeEvent() );
    Go( &PL_SELECTION_TOOL::RemoveItemsFromSel,    PL_ACTIONS::removeItemsFromSel.MakeEvent() );
    Go( &PL_SELECTION_TOOL::SelectionMenu,         PL_ACTIONS::selectionMenu.MakeEvent() );
}


