/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2023 KiCad Developers, see AUTHORS.txt for contributors.
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
#include <confirm.h>
#include <wx/textdlg.h>
#include <dialogs/dialog_grid_settings.h>
#include <widgets/std_bitmap_button.h>
#include <common.h>
#include <settings/app_settings.h>
#include <eda_draw_frame.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tool/grid_menu.h>
#include <tool/common_tools.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>

DIALOG_GRID_SETTINGS::DIALOG_GRID_SETTINGS( EDA_DRAW_FRAME* aParent ) :
        DIALOG_GRID_SETTINGS_BASE( aParent ), m_parent( aParent ),
        m_gridOriginX( aParent, m_staticTextGridPosX, m_GridOriginXCtrl, m_TextPosXUnits ),
        m_gridOriginY( aParent, m_staticTextGridPosY, m_GridOriginYCtrl, m_TextPosYUnits ),
        m_gridOverrideConnectables( aParent, m_staticTextConnectables,
                                    m_GridOverrideConnectablesSize, m_staticTextConnectablesUnits ),
        m_gridOverrideWires( aParent, m_staticTextWires, m_GridOverrideWiresSize,
                             m_staticTextWiresUnits ),
        m_gridOverrideVias( aParent, m_staticTextVias, m_GridOverrideViasSize,
                            m_staticTextViasUnits ),
        m_gridOverrideText( aParent, m_staticTextText, m_GridOverrideTextSize,
                            m_staticTextTextUnits ),
        m_gridOverrideGraphics( aParent, m_staticTextGraphics, m_GridOverrideGraphicsSize,
                                m_staticTextGraphicsUnits )
{
    // Configure display origin transforms
    m_gridOriginX.SetCoordType( ORIGIN_TRANSFORMS::ABS_X_COORD );
    m_gridOriginY.SetCoordType( ORIGIN_TRANSFORMS::ABS_Y_COORD );

    RebuildGridSizes();

    if( m_parent->IsType( FRAME_SCH )
        || m_parent->IsType( FRAME_SCH_SYMBOL_EDITOR )
        || m_parent->IsType( FRAME_SCH_VIEWER )
        || m_parent->IsType( FRAME_SCH_VIEWER_MODAL )
        || m_parent->IsType( FRAME_SIMULATOR ) )
    {
        // Eeschema and friends don't use grid origin
        sbGridOriginSizer->ShowItems( false );

        // No vias in the schematics
        m_GridOverrideViasSize->SetValue( wxT( "50 mil" ) );
        m_checkGridOverrideVias->Hide();
        m_staticTextVias->Hide();
        m_GridOverrideViasSize->Hide();
        m_staticTextViasUnits->Hide();
    }
    else
    {
        m_staticTextConnectables->SetLabel( wxT( "Footprints/pads:" ) );
        m_staticTextWires->SetLabel( wxT( "Tracks:" ) );
    }

    int hk1 = ACTIONS::gridFast1.GetHotKey();
    int hk2 = ACTIONS::gridFast2.GetHotKey();
    m_grid1HotKey->SetLabel( wxString::Format( wxT( "(%s)" ), KeyNameFromKeyCode( hk1 ) ) );
    m_grid2HotKey->SetLabel( wxString::Format( wxT( "(%s)" ), KeyNameFromKeyCode( hk2 ) ) );

    m_addGridButton->SetBitmap( KiBitmap( BITMAPS::small_plus ) );
    m_removeGridButton->SetBitmap( KiBitmap( BITMAPS::small_trash ) );

    SetupStandardButtons();
    SetInitialFocus( m_GridOriginXCtrl );

    Layout();

    // Now all widgets have the size fixed, call FinishDialogSettings
    finishDialogSettings();

    m_buttonResetSizes->Bind( wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                APP_SETTINGS_BASE* settings   = m_parent->config();
                settings->m_Window.grid.sizes = settings->DefaultGridSizeList();
                RebuildGridSizes();
                settings->m_Window.grid.last_size_idx = m_currentGridCtrl->GetSelection();
            } );
}


void DIALOG_GRID_SETTINGS::RebuildGridSizes()
{
    APP_SETTINGS_BASE* settings = m_parent->config();

    wxString savedCurrentGrid = m_currentGridCtrl->GetStringSelection();
    wxString savedGrid1       = m_grid1Ctrl->GetStringSelection();
    wxString savedGrid2       = m_grid2Ctrl->GetStringSelection();

    wxArrayString grids;
    GRID_MENU::BuildChoiceList( &grids, settings, m_parent );

    m_currentGridCtrl->Set( grids );
    m_grid1Ctrl->Set( grids );
    m_grid2Ctrl->Set( grids );

    if( !m_currentGridCtrl->SetStringSelection( savedCurrentGrid ) )
        m_currentGridCtrl->SetStringSelection( grids.front() );

    if( !m_grid1Ctrl->SetStringSelection( savedGrid1 ) )
        m_grid1Ctrl->SetStringSelection( grids.front() );

    if( !m_grid2Ctrl->SetStringSelection( savedGrid2 ) )
        m_grid2Ctrl->SetStringSelection( grids.back() );
}


bool DIALOG_GRID_SETTINGS::TransferDataFromWindow()
{
    // Validate new settings
    for( UNIT_BINDER* entry : { &m_gridOverrideConnectables, &m_gridOverrideWires,
                                &m_gridOverrideVias, &m_gridOverrideText, &m_gridOverrideGraphics } )
    {
        if( !entry->Validate( 0.001, 1000.0, EDA_UNITS::MILLIMETRES ) )
            return false;
    }

    // Apply the new settings
    APP_SETTINGS_BASE* cfg = m_parent->config();
    GRID_SETTINGS&     gridCfg = cfg->m_Window.grid;

    gridCfg.last_size_idx = m_currentGridCtrl->GetSelection();
    m_parent->SetGridOrigin( VECTOR2I( m_gridOriginX.GetValue(), m_gridOriginY.GetValue() ) );
    gridCfg.fast_grid_1 = m_grid1Ctrl->GetSelection();
    gridCfg.fast_grid_2 = m_grid2Ctrl->GetSelection();

    gridCfg.override_connectables = m_checkGridOverrideConnectables->GetValue();
    gridCfg.override_connectables_size =
            m_parent->StringFromValue( m_gridOverrideConnectables.GetValue(), true );
    gridCfg.override_wires = m_checkGridOverrideWires->GetValue();
    gridCfg.override_wires_size = m_parent->StringFromValue( m_gridOverrideWires.GetValue(), true );
    gridCfg.override_vias = m_checkGridOverrideVias->GetValue();
    gridCfg.override_vias_size = m_parent->StringFromValue( m_gridOverrideVias.GetValue(), true );
    gridCfg.override_text = m_checkGridOverrideText->GetValue();
    gridCfg.override_text_size = m_parent->StringFromValue( m_gridOverrideText.GetValue(), true );
    gridCfg.override_graphics = m_checkGridOverrideGraphics->GetValue();
    gridCfg.override_graphics_size =
            m_parent->StringFromValue( m_gridOverrideGraphics.GetValue(), true );

    // Notify TOOLS
    TOOL_MANAGER* mgr = m_parent->GetToolManager();
    mgr->ResetTools( TOOL_BASE::REDRAW );

    // Notify GAL
    mgr->RunAction( ACTIONS::gridPreset, gridCfg.last_size_idx );
    mgr->RunAction( ACTIONS::gridSetOrigin, new VECTOR2D( m_parent->GetGridOrigin() ) );

    m_parent->UpdateGridSelectBox();

    return wxDialog::TransferDataFromWindow();
}


bool DIALOG_GRID_SETTINGS::TransferDataToWindow()
{
    APP_SETTINGS_BASE* settings = m_parent->config();

    GRID_SETTINGS& gridCfg = settings->m_Window.grid;

    Layout();

    m_currentGridCtrl->SetSelection( settings->m_Window.grid.last_size_idx );

    m_gridOverrideConnectables.SetValue(
            m_parent->ValueFromString( gridCfg.override_connectables_size ) );
    m_gridOverrideWires.SetValue( m_parent->ValueFromString( gridCfg.override_wires_size ) );
    m_gridOverrideVias.SetValue( m_parent->ValueFromString( gridCfg.override_vias_size ) );
    m_gridOverrideText.SetValue( m_parent->ValueFromString( gridCfg.override_text_size ) );
    m_gridOverrideGraphics.SetValue( m_parent->ValueFromString( gridCfg.override_graphics_size ) );

    m_checkGridOverrideConnectables->SetValue( gridCfg.override_connectables );
    m_checkGridOverrideWires->SetValue( gridCfg.override_wires );
    m_checkGridOverrideVias->SetValue( gridCfg.override_vias );
    m_checkGridOverrideText->SetValue( gridCfg.override_text );
    m_checkGridOverrideGraphics->SetValue( gridCfg.override_graphics );

    m_gridOriginX.SetValue( m_parent->GetGridOrigin().x );
    m_gridOriginY.SetValue( m_parent->GetGridOrigin().y );

    m_grid1Ctrl->SetSelection( gridCfg.fast_grid_1 );
    m_grid2Ctrl->SetSelection( gridCfg.fast_grid_2 );

    return wxDialog::TransferDataToWindow();
}


void DIALOG_GRID_SETTINGS::OnAddGrid( wxCommandEvent& event )
{
    wxTextEntryDialog dlg( this, _( "New grid:" ), _( "Add Grid" ) );

    if( dlg.ShowModal() != wxID_OK )
        return;

    int            row = m_currentGridCtrl->GetSelection();
    GRID_SETTINGS& gridCfg = m_parent->config()->m_Window.grid;
    EDA_IU_SCALE   scale = m_parent->GetIuScale();
    EDA_UNITS      units = m_parent->GetUserUnits();
    double         gridSize = EDA_UNIT_UTILS::UI::DoubleValueFromString( scale, units, dlg.GetValue() );


    if( gridSize == 0.0f )
    {
        DisplayError( this, _( "Grid must have a valid size." ) );
        return;
    }

    wxString gridSizeStr =
            EDA_UNIT_UTILS::UI::StringFromValue( scale, EDA_UNITS::MILLIMETRES, gridSize );

    for( const wxString& size : gridCfg.sizes )
    {
        if( gridSizeStr == size )
        {
            DisplayError( this,
                          wxString::Format( _( "Grid size '%s' already exists." ), gridSizeStr ) );
            return;
        }
    }

    gridCfg.sizes.insert( gridCfg.sizes.begin() + row, gridSizeStr );
    RebuildGridSizes();
    m_currentGridCtrl->SetSelection( row );
}


void DIALOG_GRID_SETTINGS::OnRemoveGrid( wxCommandEvent& event )
{
    GRID_SETTINGS& gridCfg = m_parent->config()->m_Window.grid;
    int            row = m_currentGridCtrl->GetSelection();

    if( gridCfg.sizes.size() <= 1 )
    {
        DisplayError( this, wxString::Format( _( "At least one grid size is required." ) ) );
        return;
    }

    gridCfg.sizes.erase( gridCfg.sizes.begin() + row );
    RebuildGridSizes();

    if( row != 0 )
        m_currentGridCtrl->SetSelection( row - 1 );
}


