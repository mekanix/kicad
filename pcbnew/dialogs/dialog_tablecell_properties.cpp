/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <widgets/bitmap_button.h>
#include <widgets/font_choice.h>
#include <confirm.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <board.h>
#include <footprint.h>
#include <pcb_textbox.h>
#include <pcb_tablecell.h>
#include <pcb_table.h>
#include <project.h>
#include <pcb_edit_frame.h>
#include <pcb_layer_box_selector.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <scintilla_tricks.h>
#include "dialog_tablecell_properties.h"

class TABLECELL_SCINTILLA_TRICKS : public SCINTILLA_TRICKS
{
public:
    TABLECELL_SCINTILLA_TRICKS( wxStyledTextCtrl* aScintilla,
                                std::function<void( wxKeyEvent& )> onAcceptHandler,
                                std::function<void()> onNextHandler ) :
            SCINTILLA_TRICKS( aScintilla, wxT( "{}" ), false, std::move( onAcceptHandler ) ),
            m_onNextHandler( std::move( onNextHandler ) )
    { }

protected:
    void onCharHook( wxKeyEvent& aEvent ) override
    {
        if( aEvent.GetKeyCode() == WXK_TAB && aEvent.AltDown() && !aEvent.ControlDown() )
            m_onNextHandler();
        else
            SCINTILLA_TRICKS::onCharHook( aEvent );
    }

private:
    std::function<void()> m_onNextHandler;
};


DIALOG_TABLECELL_PROPERTIES::DIALOG_TABLECELL_PROPERTIES( PCB_BASE_EDIT_FRAME* aFrame,
                                                          PCB_TABLECELL* aCell ) :
        DIALOG_TABLECELL_PROPERTIES_BASE( aFrame ),
        m_frame( aFrame ),
        m_table( nullptr ),
        m_cell( aCell ),
        m_borderWidth( aFrame, m_borderWidthLabel, m_borderWidthCtrl, m_borderWidthUnits ),
        m_separatorsWidth( aFrame, m_separatorsWidthLabel, m_separatorsWidthCtrl, m_separatorsWidthUnits ),
        m_textHeight( aFrame, m_SizeYLabel, m_SizeYCtrl, m_SizeYUnits ),
        m_textWidth( aFrame, m_SizeXLabel, m_SizeXCtrl, m_SizeXUnits ),
        m_textThickness( aFrame, m_ThicknessLabel, m_ThicknessCtrl, m_ThicknessUnits ),
        m_marginLeft( aFrame, nullptr, m_marginLeftCtrl, nullptr ),
        m_marginTop( aFrame, nullptr, m_marginTopCtrl, m_marginTopUnits ),
        m_marginRight( aFrame, nullptr, m_marginRightCtrl, nullptr ),
        m_marginBottom( aFrame, nullptr, m_marginBottomCtrl, nullptr ),
        m_scintillaTricks( nullptr )
{
    m_table = static_cast<PCB_TABLE*>( m_cell->GetParent() );

#ifdef _WIN32
    // Without this setting, on Windows, some esoteric unicode chars create display issue
    // in a wxStyledTextCtrl.
    // for SetTechnology() info, see https://www.scintilla.org/ScintillaDoc.html#SCI_SETTECHNOLOGY
    m_textCtrl->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);
#endif

    m_scintillaTricks = new TABLECELL_SCINTILLA_TRICKS( m_textCtrl,
            // onAccept handler
            [this]( wxKeyEvent& aEvent )
            {
                wxPostEvent( this, wxCommandEvent( wxEVT_COMMAND_BUTTON_CLICKED, wxID_OK ) );
            },
            // onNext handler
            [this]()
            {
                wxCommandEvent dummy;
                OnApply( dummy );
            } );

    // A hack which causes Scintilla to auto-size the text editor canvas
    // See: https://github.com/jacobslusser/ScintillaNET/issues/216
    m_textCtrl->SetScrollWidth( 1 );
    m_textCtrl->SetScrollWidthTracking( true );

    SetInitialFocus( m_textCtrl );

    if( m_table->GetParentFootprint() )
    {
        // Do not allow locking items in the footprint editor
        m_cbLocked->Show( false );
    }

    // Configure the layers list selector.  Note that footprints are built outside the current
    // board and so we may need to show all layers if the text is on an unactivated layer.
    if( !m_frame->GetBoard()->IsLayerEnabled( m_table->GetLayer() ) )
        m_LayerSelectionCtrl->ShowNonActivatedLayers( true );

    m_LayerSelectionCtrl->SetLayersHotkeys( false );
    m_LayerSelectionCtrl->SetBoardFrame( m_frame );
    m_LayerSelectionCtrl->Resync();

    for( const auto& [lineStyle, lineStyleDesc] : lineTypeNames )
    {
        m_borderStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
        m_separatorsStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
    }

    m_borderStyleCombo->Append( DEFAULT_STYLE );
    m_separatorsStyleCombo->Append( DEFAULT_STYLE );

    m_separator1->SetIsSeparator();

    m_bold->SetIsCheckButton();
    m_bold->SetBitmap( KiBitmapBundle( BITMAPS::text_bold ) );
    m_italic->SetIsCheckButton();
    m_italic->SetBitmap( KiBitmapBundle( BITMAPS::text_italic ) );

    m_separator2->SetIsSeparator();

    m_hAlignLeft->SetIsRadioButton();
    m_hAlignLeft->SetBitmap( KiBitmapBundle( BITMAPS::text_align_left ) );
    m_hAlignCenter->SetIsRadioButton();
    m_hAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_align_center ) );
    m_hAlignRight->SetIsRadioButton();
    m_hAlignRight->SetBitmap( KiBitmapBundle( BITMAPS::text_align_right ) );

    m_separator3->SetIsSeparator();

    m_vAlignTop->SetIsRadioButton();
    m_vAlignTop->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_top ) );
    m_vAlignCenter->SetIsRadioButton();
    m_vAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_center ) );
    m_vAlignBottom->SetIsRadioButton();
    m_vAlignBottom->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_bottom ) );

    m_separator4->SetIsSeparator();

    m_hotkeyHint->SetFont( KIUI::GetInfoFont( this ) );
    m_hotkeyHint->SetLabel( wxString::Format( wxT( "(%s+%s)" ),
                                              KeyNameFromKeyCode( WXK_ALT ),
                                              KeyNameFromKeyCode( WXK_TAB ) ) );

    SetupStandardButtons();
    Layout();

    m_hAlignLeft->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_hAlignCenter->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_hAlignRight->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_vAlignTop->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );
    m_vAlignCenter->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );
    m_vAlignBottom->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );

    // Now all widgets have the size fixed, call FinishDialogSettings
    finishDialogSettings();
}


DIALOG_TABLECELL_PROPERTIES::~DIALOG_TABLECELL_PROPERTIES()
{
    delete m_scintillaTricks;
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataToWindow()
{
    if( !wxDialog::TransferDataToWindow() )
        return false;

    m_LayerSelectionCtrl->SetLayerSelection( m_table->GetLayer() );
    m_cbLocked->SetValue( m_table->IsLocked() );

    m_borderCheckbox->SetValue( m_table->StrokeExternal() );
    m_headerBorder->SetValue( m_table->StrokeHeader() );

    if( m_table->GetBorderStroke().GetWidth() >= 0 )
        m_borderWidth.SetValue( m_table->GetBorderStroke().GetWidth() );

    int style = static_cast<int>( m_table->GetBorderStroke().GetLineStyle() );

    if( style == -1 )
        m_borderStyleCombo->SetStringSelection( DEFAULT_STYLE );
    else if( style < (int) lineTypeNames.size() )
        m_borderStyleCombo->SetSelection( style );
    else
        wxFAIL_MSG( "Line type not found in the type lookup map" );

    m_borderWidth.Enable( m_table->StrokeExternal() || m_table->StrokeHeader() );
    m_borderStyleLabel->Enable( m_table->StrokeExternal() || m_table->StrokeHeader() );
    m_borderStyleCombo->Enable( m_table->StrokeExternal() || m_table->StrokeHeader() );

    bool rows = m_table->StrokeRows() && m_table->GetSeparatorsStroke().GetWidth() >= 0;
    bool cols = m_table->StrokeColumns() && m_table->GetSeparatorsStroke().GetWidth() >= 0;

    m_rowSeparators->SetValue( rows );
    m_colSeparators->SetValue( cols );

    if( m_table->GetSeparatorsStroke().GetWidth() >= 0 )
        m_separatorsWidth.SetValue( m_table->GetSeparatorsStroke().GetWidth() );

    style = static_cast<int>( m_table->GetSeparatorsStroke().GetLineStyle() );

    if( style == -1 )
        m_separatorsStyleCombo->SetStringSelection( DEFAULT_STYLE );
    else if( style < (int) lineTypeNames.size() )
        m_separatorsStyleCombo->SetSelection( style );
    else
        wxFAIL_MSG( "Line type not found in the type lookup map" );

    m_separatorsWidth.Enable( rows || cols );
    m_separatorsStyleLabel->Enable( rows || cols );
    m_separatorsStyleCombo->Enable( rows || cols );

    m_textCtrl->SetValue( m_cell->GetText() );
    m_fontCtrl->SetFontSelection( m_cell->GetFont() );
    m_textWidth.SetValue( m_cell->GetTextWidth() );
    m_textHeight.SetValue( m_cell->GetTextHeight() );
    m_textThickness.SetValue( m_cell->GetTextThickness() );

    m_bold->Check( m_cell->IsBold() );
    m_italic->Check( m_cell->IsItalic() );

    switch( m_cell->GetHorizJustify() )
    {
    case GR_TEXT_H_ALIGN_LEFT:   m_hAlignLeft->Check();   break;
    case GR_TEXT_H_ALIGN_CENTER: m_hAlignCenter->Check(); break;
    case GR_TEXT_H_ALIGN_RIGHT:  m_hAlignRight->Check();  break;
    }

    switch( m_cell->GetVertJustify() )
    {
    case GR_TEXT_V_ALIGN_TOP:    m_vAlignTop->Check();    break;
    case GR_TEXT_V_ALIGN_CENTER: m_vAlignCenter->Check(); break;
    case GR_TEXT_V_ALIGN_BOTTOM: m_vAlignBottom->Check(); break;
    }

    m_marginLeft.SetValue( m_cell->GetMarginLeft() );
    m_marginTop.SetValue( m_cell->GetMarginTop() );
    m_marginRight.SetValue( m_cell->GetMarginRight() );
    m_marginBottom.SetValue( m_cell->GetMarginBottom() );

    return true;
}


void DIALOG_TABLECELL_PROPERTIES::onHAlignButton( wxCommandEvent& aEvent )
{
    for( BITMAP_BUTTON* btn : { m_hAlignLeft, m_hAlignCenter, m_hAlignRight } )
    {
        if( btn->IsChecked() && btn != aEvent.GetEventObject() )
            btn->Check( false );
    }
}


void DIALOG_TABLECELL_PROPERTIES::onVAlignButton( wxCommandEvent& aEvent )
{
    for( BITMAP_BUTTON* btn : { m_vAlignTop, m_vAlignCenter, m_vAlignBottom } )
    {
        if( btn->IsChecked() && btn != aEvent.GetEventObject() )
            btn->Check( false );
    }
}


void DIALOG_TABLECELL_PROPERTIES::onBorderChecked( wxCommandEvent& aEvent )
{
    BOARD_DESIGN_SETTINGS& bds = m_frame->GetDesignSettings();
    PCB_LAYER_ID           currentLayer = ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() );
    int                    defaultLineThickness = bds.GetLineThickness( currentLayer );

    bool border = m_borderCheckbox->GetValue();

    if( border && m_borderWidth.GetValue() < 0 )
        m_borderWidth.SetValue( defaultLineThickness );

    m_borderWidth.Enable( border );
    m_borderStyleLabel->Enable( border );
    m_borderStyleCombo->Enable( border );

    bool row = m_rowSeparators->GetValue();
    bool col = m_colSeparators->GetValue();

    if( ( row || col ) && m_separatorsWidth.GetValue() < 0 )
        m_separatorsWidth.SetValue( defaultLineThickness );

    m_separatorsWidth.Enable( row || col );
    m_separatorsStyleLabel->Enable( row || col );
    m_separatorsStyleCombo->Enable( row || col );
}


void DIALOG_TABLECELL_PROPERTIES::OnCharHook( wxKeyEvent& aEvt )
{
    if( aEvt.GetKeyCode() == WXK_TAB && aEvt.AltDown() && !aEvt.ControlDown() )
    {
        wxCommandEvent dummy;
        OnApply( dummy );
    }
    else
    {
        DIALOG_SHIM::OnCharHook( aEvt );
    }
}


void DIALOG_TABLECELL_PROPERTIES::OnApply( wxCommandEvent& aEvent )
{
    TransferDataFromWindow();

    for( size_t ii = 0; ii < m_table->GetCells().size(); ++ii )
    {
        if( m_table->GetCells()[ii] == m_cell )
        {
            ii++;

            if( ii >= m_table->GetCells().size() )
                ii = 0;

            m_cell = m_table->GetCells()[ii];

            m_frame->GetToolManager()->RunAction( PCB_ACTIONS::selectionClear );
            m_frame->GetToolManager()->RunAction<EDA_ITEM*>( PCB_ACTIONS::selectItem, m_cell );
            break;
        }
    }

    TransferDataToWindow();
    m_textCtrl->SelectAll();
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataFromWindow()
{
    if( !wxDialog::TransferDataFromWindow() )
        return false;

    BOARD_COMMIT commit( m_frame );
    commit.Modify( m_table );

    // If no other command in progress, prepare undo command
    // (for a command in progress, will be made later, at the completion of command)
    bool pushCommit = ( m_table->GetEditFlags() == 0 );

    // Set IN_EDIT flag to force undo/redo/abort proper operation and avoid new calls to
    // SaveCopyInUndoList for the same text if is moved, and then rotated, edited, etc....
    if( !pushCommit )
        m_table->SetFlags( IN_EDIT );

    m_table->SetLayer( ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() ) );
    m_table->SetLocked( m_cbLocked->GetValue() );

    m_table->SetStrokeExternal( m_borderCheckbox->GetValue() );
    m_table->SetStrokeHeader( m_headerBorder->GetValue() );
    {
        STROKE_PARAMS stroke = m_table->GetBorderStroke();

        if( m_borderCheckbox->GetValue() )
            stroke.SetWidth( std::max( 0, m_borderWidth.GetIntValue() ) );
        else
            stroke.SetWidth( -1 );

        auto it = lineTypeNames.begin();
        std::advance( it, m_borderStyleCombo->GetSelection() );

        if( it == lineTypeNames.end() )
            stroke.SetLineStyle( LINE_STYLE::DEFAULT );
        else
            stroke.SetLineStyle( it->first );

        m_table->SetBorderStroke( stroke );
    }

    m_table->SetStrokeRows( m_rowSeparators->GetValue() );
    m_table->SetStrokeColumns( m_colSeparators->GetValue() );
    {
        STROKE_PARAMS stroke = m_table->GetSeparatorsStroke();

        if( m_rowSeparators->GetValue() || m_colSeparators->GetValue() )
            stroke.SetWidth( std::max( 0, m_separatorsWidth.GetIntValue() ) );
        else
            stroke.SetWidth( -1 );

        auto it = lineTypeNames.begin();
        std::advance( it, m_separatorsStyleCombo->GetSelection() );

        if( it == lineTypeNames.end() )
            stroke.SetLineStyle( LINE_STYLE::DEFAULT );
        else
            stroke.SetLineStyle( it->first );

        m_table->SetSeparatorsStroke( stroke );
    }

    wxString txt = m_textCtrl->GetValue();

#ifdef __WXMAC__
    // On macOS CTRL+Enter produces '\r' instead of '\n' regardless of EOL setting.
    // Replace it now.
    txt.Replace( "\r", "\n" );
#elif defined( __WINDOWS__ )
    // On Windows, a new line is coded as \r\n.  We use only \n in kicad files and in
    // drawing routines so strip the \r char.
    txt.Replace( "\r", "" );
#endif

    m_cell->SetText( txt );

    if( m_fontCtrl->HaveFontSelection() )
    {
        m_cell->SetFont( m_fontCtrl->GetFontSelection( m_bold->IsChecked(),
                                                       m_italic->IsChecked() ) );
    }

    m_cell->SetTextWidth( m_textWidth.GetIntValue() );
    m_cell->SetTextHeight( m_textHeight.GetIntValue() );
    m_cell->SetTextThickness( m_textThickness.GetIntValue() );

    if( m_bold->IsChecked() != m_cell->IsBold() )
    {
        if( m_bold->IsChecked() )
        {
            m_cell->SetBold( true );
            m_cell->SetTextThickness( GetPenSizeForBold( m_cell->GetTextWidth() ) );
        }
        else
        {
            m_cell->SetBold( false );
            m_cell->SetTextThickness( 0 ); // Use default pen width
        }
    }

    if( m_hAlignRight->IsChecked() )
        m_cell->SetHorizJustify( GR_TEXT_H_ALIGN_RIGHT );
    else if( m_hAlignCenter->IsChecked() )
        m_cell->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER );
    else
        m_cell->SetHorizJustify( GR_TEXT_H_ALIGN_LEFT );

    if( m_vAlignBottom->IsChecked() )
        m_cell->SetVertJustify( GR_TEXT_V_ALIGN_BOTTOM );
    else if( m_vAlignCenter->IsChecked() )
        m_cell->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );
    else
        m_cell->SetVertJustify( GR_TEXT_V_ALIGN_TOP );

    m_cell->SetMarginLeft( m_marginLeft.GetIntValue() );
    m_cell->SetMarginTop( m_marginTop.GetIntValue() );
    m_cell->SetMarginRight( m_marginRight.GetIntValue() );
    m_cell->SetMarginBottom( m_marginBottom.GetIntValue() );

    if( !commit.Empty() )
        commit.Push( _( "Edit Table Cell" ), SKIP_CONNECTIVITY );

    return true;
}


void PCB_BASE_EDIT_FRAME::ShowTableCellPropertiesDialog( PCB_TABLECELL* aTableCell )
{
    DIALOG_TABLECELL_PROPERTIES dlg( this, aTableCell );

    // QuasiModal required for Scintilla auto-complete
    dlg.ShowQuasiModal();
}


