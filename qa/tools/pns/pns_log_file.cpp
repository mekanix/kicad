/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020-2023 KiCad Developers.
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


// WARNING - this Tom's crappy PNS hack tool code. Please don't complain about its quality
// (unless you want to improve it).

#include "pns_log_file.h"

#include <router/pns_segment.h>

#include <board_design_settings.h>

#include <pcbnew/plugins/kicad/pcb_plugin.h>
#include <pcbnew/drc/drc_engine.h>

#include <project.h>
#include <project/project_local_settings.h>

#include <../../tests/common/console_log.h>

BOARD_CONNECTED_ITEM* PNS_LOG_FILE::ItemById( const PNS::LOGGER::EVENT_ENTRY& evt )
{
    BOARD_CONNECTED_ITEM* parent = nullptr;

    for( BOARD_CONNECTED_ITEM* item : m_board->AllConnectedItems() )
    {
        if( item->m_Uuid == evt.uuid )
        {
            parent = item;
            break;
        };
    }

    return parent;
}

static const wxString readLine( FILE* f )
{
    char str[16384];
    fgets( str, sizeof( str ) - 1, f );
    return wxString( str );
}


PNS_LOG_FILE::PNS_LOG_FILE() :
    m_mode( PNS::ROUTER_MODE::PNS_MODE_ROUTE_SINGLE )
{
    m_routerSettings.reset( new PNS::ROUTING_SETTINGS( nullptr, "" ) );
}

std::shared_ptr<SHAPE> parseShape( SHAPE_TYPE expectedType, wxStringTokenizer& aTokens )
{
    SHAPE_TYPE type = static_cast<SHAPE_TYPE> ( wxAtoi( aTokens.GetNextToken() ) );

    if( type == SHAPE_TYPE::SH_SEGMENT )
    {
        std::shared_ptr<SHAPE_SEGMENT> sh( new SHAPE_SEGMENT );
        VECTOR2I a, b;
        a.x = wxAtoi( aTokens.GetNextToken() );
        a.y = wxAtoi( aTokens.GetNextToken() );
        b.x = wxAtoi( aTokens.GetNextToken() );
        b.y = wxAtoi( aTokens.GetNextToken() );
        int width = wxAtoi( aTokens.GetNextToken() );
        sh->SetSeg( SEG( a, b ));
        sh->SetWidth( width );
        return sh;
    }
    else if( type == SHAPE_TYPE::SH_CIRCLE )
    {
        std::shared_ptr<SHAPE_CIRCLE> sh(  new SHAPE_CIRCLE );
        VECTOR2I a;
        a.x = wxAtoi( aTokens.GetNextToken() );
        a.y = wxAtoi( aTokens.GetNextToken() );
        int radius = wxAtoi( aTokens.GetNextToken() );
        sh->SetCenter( a );
        sh->SetRadius( radius );
        return sh;
    }

    return nullptr;
}

bool PNS_LOG_FILE::parseCommonPnsProps( PNS::ITEM* aItem, const wxString& cmd,
                                        wxStringTokenizer& aTokens )
{
    if( cmd == wxS( "net" ) )
    {
        if( aItem->Parent() && aItem->Parent()->GetBoard() )
        {
            aItem->SetNet( m_board->FindNet( aTokens.GetNextToken() ) );
            return true;
        }

        return false;
    }
    else if( cmd == wxS( "layers" ) )
    {
        int start = wxAtoi( aTokens.GetNextToken() );
        int end = wxAtoi( aTokens.GetNextToken() );
        aItem->SetLayers( LAYER_RANGE( start, end ) );
        return true;
    }
    return false;
}

PNS::SEGMENT* PNS_LOG_FILE::parsePnsSegmentFromString( PNS::SEGMENT* aSeg,
                                                       wxStringTokenizer& aTokens )
{
    PNS::SEGMENT* seg = new PNS::SEGMENT();

    while( aTokens.CountTokens() )
    {
        wxString cmd = aTokens.GetNextToken();

        if( !parseCommonPnsProps( seg, cmd, aTokens ) )
        {
            if( cmd == wxS( "shape" ) )
            {
                std::shared_ptr<SHAPE> sh = parseShape( SH_SEGMENT, aTokens );

                if( !sh )
                    return nullptr;

                seg->SetShape( *static_cast<SHAPE_SEGMENT*>(sh.get()) );

            }
        }
    }

    return seg;
}

PNS::VIA* PNS_LOG_FILE::parsePnsViaFromString( PNS::VIA* aSeg, wxStringTokenizer& aTokens )
{
    PNS::VIA* via = new PNS::VIA();

    while( aTokens.CountTokens() )
    {
        wxString cmd = aTokens.GetNextToken();

        if( !parseCommonPnsProps( via, cmd, aTokens ) )
        {
            if( cmd == wxS( "shape" ) )
            {
                std::shared_ptr<SHAPE> sh = parseShape( SH_CIRCLE, aTokens );

                if( !sh )
                    return nullptr;

                SHAPE_CIRCLE* sc = static_cast<SHAPE_CIRCLE*>( sh.get() );

                via->SetPos( sc->GetCenter() );
                via->SetDiameter( 2 * sc->GetRadius() );
            }
            else if( cmd == wxS( "drill" ) )
            {
                via->SetDrill( wxAtoi( aTokens.GetNextToken() ) );
            }
        }
    }

    return via;
}


PNS::ITEM* PNS_LOG_FILE::parseItemFromString( wxStringTokenizer& aTokens )
{
    wxString type = aTokens.GetNextToken();

    if( type == wxS( "segment" ) )
    {
        PNS::SEGMENT* seg = new PNS::SEGMENT();
        return parsePnsSegmentFromString( seg, aTokens );
    }
    else if( type == wxS( "via" ) )
    {
        PNS::VIA* seg = new PNS::VIA();
        return parsePnsViaFromString( seg, aTokens );
    }

    return nullptr;
}

bool comparePnsItems( const PNS::ITEM* a , const PNS::ITEM* b )
{
    if( a->Kind() != b->Kind() )
        return false;

    if( a->Net() != b->Net() )
        return false;

    if( a->Layers() != b->Layers() )
        return false;

    if( a->Kind() == PNS::ITEM::VIA_T )
    {
        const PNS::VIA* va = static_cast<const PNS::VIA*>(a);
        const PNS::VIA* vb = static_cast<const PNS::VIA*>(b);

        if( va->Diameter() != vb->Diameter() )
            return false;

        if( va->Drill() != vb->Drill() )
            return false;

        if( va->Pos() != vb->Pos() )
            return false;

    }
    else if ( a->Kind() == PNS::ITEM::SEGMENT_T )
    {
        const PNS::SEGMENT* sa = static_cast<const PNS::SEGMENT*>(a);
        const PNS::SEGMENT* sb = static_cast<const PNS::SEGMENT*>(b);

        if( sa->Seg() != sb->Seg() )
            return false;

        if( sa->Width() != sb->Width() )
            return false;
    }

    return true;
}


const std::set<PNS::ITEM*> deduplicate( const std::vector<PNS::ITEM*>& items )
{
    std::set<PNS::ITEM*> rv;

    for( PNS::ITEM* item : items )
    {
        bool isDuplicate = false;

        for( PNS::ITEM* ritem : rv )
        {
            if( comparePnsItems( ritem, item) )
            {
                isDuplicate = true;
                break;
            }

            if( !isDuplicate )
                rv.insert( item );
        }
    }

    return rv;
}


bool PNS_LOG_FILE::COMMIT_STATE::Compare( const PNS_LOG_FILE::COMMIT_STATE& aOther )
{
    COMMIT_STATE check( aOther );

    //printf("pre-compare: %d/%d\n", check.m_addedItems.size(), check.m_removedIds.size() );
    //printf("pre-compare (log): %d/%d\n", m_addedItems.size(), m_removedIds.size() );

    for( const KIID& uuid : m_removedIds )
    {
        if( check.m_removedIds.find( uuid ) != check.m_removedIds.end() )
            check.m_removedIds.erase( uuid );
        else
            return false; // removed twice? wtf
    }

    std::set<PNS::ITEM*> addedItems = deduplicate( m_addedItems );
    std::set<PNS::ITEM*> chkAddedItems = deduplicate( check.m_addedItems );

    for( PNS::ITEM* item : addedItems )
    {
        for( PNS::ITEM* chk : chkAddedItems )
        {
            if( comparePnsItems( item, chk ) )
            {
                chkAddedItems.erase( chk );
                break;
            }
        }
    }

    //printf("post-compare: %d/%d\n", chkAddedItems.size(), check.m_removedIds.size() );

    return chkAddedItems.empty() && check.m_removedIds.empty();
}


bool PNS_LOG_FILE::SaveLog( const wxFileName& logFileName, REPORTER* aRpt )
{
    std::vector<PNS::ITEM*> dummyHeads; // todo - save heads when we support it in QA

    FILE*    log_f = wxFopen( logFileName.GetFullPath(), "wb" );
    wxString logString = PNS::LOGGER::FormatLogFileAsString( m_mode, m_commitState.m_addedItems,
                                                             m_commitState.m_removedIds, dummyHeads,
                                                             m_events );
    fprintf( log_f, "%s\n", logString.c_str().AsChar() );
    fclose( log_f );

    return true;
}


bool PNS_LOG_FILE::Load( const wxFileName& logFileName, REPORTER* aRpt )
{
    wxFileName fname_log( logFileName );
    fname_log.SetExt( wxT( "log" ) );

    wxFileName fname_dump( logFileName );
    fname_dump.SetExt( wxT( "dump" ) );

    wxFileName fname_project( logFileName );
    fname_project.SetExt( wxT( "kicad_pro" ) );
    fname_project.MakeAbsolute();

    wxFileName fname_settings( logFileName );
    fname_settings.SetExt( wxT( "settings" ) );

    aRpt->Report( wxString::Format( wxT( "Loading router settings from '%s'" ),
                                    fname_settings.GetFullPath() ) );

    bool ok = m_routerSettings->LoadFromRawFile( fname_settings.GetFullPath() );

    if( !ok )
    {
        aRpt->Report( wxString::Format( wxT( "Failed to load routing settings. Usign defaults." ) ) ,
                      RPT_SEVERITY_WARNING );
    }

    aRpt->Report( wxString::Format( wxT( "Loading project settings from '%s'" ),
                                    fname_settings.GetFullPath() ) );

    m_settingsMgr.reset( new SETTINGS_MANAGER ( true ) );
    m_settingsMgr->LoadProject( fname_project.GetFullPath() );
    PROJECT* project = m_settingsMgr->GetProject( fname_project.GetFullPath() );
    project->SetReadOnly();

    try
    {
        PCB_PLUGIN io;
        aRpt->Report( wxString::Format( wxT("Loading board snapshot from '%s'"), fname_dump.GetFullPath() ) );

        m_board.reset( io.LoadBoard( fname_dump.GetFullPath(), nullptr, nullptr ) );
        m_board->SetProject( project );

        std::shared_ptr<DRC_ENGINE> drcEngine( new DRC_ENGINE );

        CONSOLE_LOG            consoleLog;
        BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();

        bds.m_DRCEngine = drcEngine;
        bds.m_UseConnectedTrackWidth = project->GetLocalSettings().m_AutoTrackWidth;

        m_board->SynchronizeNetsAndNetClasses( true );

        drcEngine->SetBoard( m_board.get() );
        drcEngine->SetDesignSettings( &bds );
        drcEngine->SetLogReporter( new CONSOLE_MSG_REPORTER( &consoleLog ) );
        drcEngine->InitEngine( wxFileName() );
    }
    catch( const PARSE_ERROR& parse_error )
    {
        aRpt->Report( wxString::Format( "parse error : %s (%s)\n", parse_error.Problem(),
                      parse_error.What() ), RPT_SEVERITY_ERROR );

        return false;
    }

    FILE* f = fopen( fname_log.GetFullPath().c_str(), "rb" );

    aRpt->Report( wxString::Format( "Loading log from '%s'", fname_log.GetFullPath() ) );

    if( !f )
    {
        aRpt->Report( wxT( "Failed to load log" ), RPT_SEVERITY_ERROR );
        return false;
    }

    while( !feof( f ) )
    {
        wxString line = readLine( f );
        wxStringTokenizer tokens( line );

        if( !tokens.CountTokens() )
            continue;

        wxString cmd = tokens.GetNextToken();

        if( cmd == wxT( "mode" ) )
        {
            m_mode = static_cast<PNS::ROUTER_MODE>( wxAtoi( tokens.GetNextToken() ) );
        }
        else if( cmd == wxT( "event" ) )
        {
            m_events.push_back( PNS::LOGGER::ParseEvent( line ) );
        }
        else if ( cmd == wxT( "added" ) )
        {
            PNS::ITEM* item = parseItemFromString( tokens );
            m_commitState.m_addedItems.push_back( item );
        }
        else if ( cmd == wxT( "removed" ) )
        {
            m_commitState.m_removedIds.insert( KIID( tokens.GetNextToken() ) );
        }
    }

    fclose( f );

    return true;
}
