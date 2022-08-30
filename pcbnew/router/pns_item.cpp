/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2014 CERN
 * Copyright (C) 2016-2022 KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <zone.h>
#include "pns_node.h"
#include "pns_item.h"
#include "pns_line.h"
#include "pns_router.h"
#include "pns_utils.h"

#include <geometry/shape_compound.h>
#include <geometry/shape_poly_set.h>

typedef VECTOR2I::extended_type ecoord;

namespace PNS {

static void dumpObstacles( const PNS::NODE::OBSTACLES &obstacles )
{
    printf("&&&& %d obstacles: \n", obstacles.size() );

    for( const auto& obs : obstacles )
    {
        printf("%p [%s] - %p [%s], clearance %d\n", obs.m_head, obs.m_head->KindStr().c_str(),
                                                obs.m_item, obs.m_item->KindStr().c_str(),
                                                obs.m_clearance );
    }
}


bool ITEM::collideSimple( const ITEM* aHead, const NODE* aNode,
                          COLLISION_SEARCH_CONTEXT* aCtx ) const
{
    const SHAPE* shapeI = Shape();
    const HOLE*  holeI = Hole();
    int          lineWidthI = 0;

    const SHAPE* shapeH = aHead->Shape();
    const HOLE*  holeH = aHead->Hole();
    int          lineWidthH = 0;
    int          clearanceEpsilon = aNode->GetRuleResolver()->ClearanceEpsilon();
    bool         collisionsFound = false;

    printf("******************** CollideSimple %d\n", aCtx->obstacles.size() );

    //printf( "h %p n %p t %p ctx %p\n", aHead, aNode, this, aCtx );

    if( aHead == this )  // we cannot be self-colliding
        return false;

    // Sadly collision routines ignore SHAPE_POLY_LINE widths so we have to pass them in as part
    // of the clearance value.
    if( m_kind == LINE_T )
        lineWidthI = static_cast<const LINE*>( this )->Width() / 2;

    if( aHead->m_kind == LINE_T )
        lineWidthH = static_cast<const LINE*>( aHead )->Width() / 2;

    // same nets? no collision!
    if( aCtx && aCtx->options.m_differentNetsOnly
            && m_net == aHead->m_net && m_net >= 0 && aHead->m_net >= 0 )
    {
        return false;
    }

    // a pad associated with a "free" pin (NIC) doesn't have a net until it has been used
    if( aCtx && aCtx->options.m_differentNetsOnly
            && ( IsFreePad() || aHead->IsFreePad() ) )
    {
        return false;
    }

    // check if we are not on completely different layers first
    if( !m_layers.Overlaps( aHead->m_layers ) )
        return false;

    auto checkKeepout =
            []( const ZONE* aKeepout, const BOARD_ITEM* aOther )
            {
                if( aKeepout->GetDoNotAllowTracks() && aOther->IsType( { PCB_ARC_T, PCB_TRACE_T } ) )
                    return true;

                if( aKeepout->GetDoNotAllowVias() && aOther->Type() == PCB_VIA_T )
                    return true;

                if( aKeepout->GetDoNotAllowPads() && aOther->Type() == PCB_PAD_T )
                    return true;

                // Incomplete test, but better than nothing:
                if( aKeepout->GetDoNotAllowFootprints() && aOther->Type() == PCB_PAD_T )
                {
                    return !aKeepout->GetParentFootprint()
                            || aKeepout->GetParentFootprint() != aOther->GetParentFootprint();
                }

                return false;
            };

    const ZONE* zoneA = dynamic_cast<ZONE*>( Parent() );
    const ZONE* zoneB = dynamic_cast<ZONE*>( aHead->Parent() );

    if( zoneA && aHead->Parent() && !checkKeepout( zoneA, aHead->Parent() ) )
        return false;

    if( zoneB && Parent() && !checkKeepout( zoneB, Parent() ) )
        return false;

    // fixme: this f***ing singleton must go...
    ROUTER *router = ROUTER::GetInstance();
    ROUTER_IFACE* iface = router ? router->GetInterface() : nullptr;

    bool thisNotFlashed = false;
    bool otherNotFlashed = false;

    if( iface )
    {
        thisNotFlashed = !iface->IsFlashedOnLayer( this, aHead->Layer() );
        otherNotFlashed = !iface->IsFlashedOnLayer( aHead, Layer() );
    }

    if( ( aNode->GetCollisionQueryScope() == NODE::CQS_ALL_RULES
          || ( thisNotFlashed || otherNotFlashed ) ) )
    {
        if( holeI && holeI->ParentPadVia() != aHead && holeI != aHead )
        {
            int holeClearance = aNode->GetClearance( this, holeI );
            printf("HCH1 %d\n", holeClearance);

            if( holeI->Shape()->Collide( shapeH, holeClearance + lineWidthH - clearanceEpsilon ) )
            {
                if( aCtx )
                {
                    OBSTACLE obs;
                    obs.m_clearance = holeClearance;
                    obs.m_head = const_cast<ITEM*>( aHead );
                    obs.m_item = const_cast<HOLE*>( holeI );

                    aCtx->obstacles.insert( obs );
                    dumpObstacles( aCtx->obstacles );
                    collisionsFound = true;
                }
                else
                {
                    return true;
                }
            }
        }

        if( holeH && holeH->ParentPadVia() != this && holeH != this )
        {
            int holeClearance = aNode->GetClearance( this, holeH );

            printf("HCH2 %d\n", holeClearance);

            if( holeH->Shape()->Collide( shapeI, holeClearance + lineWidthI - clearanceEpsilon ) )
            {
                if( aCtx )
                {
                    OBSTACLE obs;
                    obs.m_clearance = holeClearance;
                    obs.m_head = const_cast<HOLE*>( holeH );
                    obs.m_item = const_cast<ITEM*>( this );

                    aCtx->obstacles.insert( obs );
                    dumpObstacles( aCtx->obstacles );
                    collisionsFound = true;
                }
                else
                {
                    return true;
                }
            }
        }

        if( holeI && holeH && ( holeI != holeH ) )
        {
            int holeClearance = aNode->GetClearance( holeI, holeH );

            printf("HCH3 %d\n", holeClearance);

            if( holeI->Shape()->Collide( holeH->Shape(), holeClearance - clearanceEpsilon ) )
            {
                if( aCtx )
                {
                    OBSTACLE obs;
                    obs.m_clearance = holeClearance;

                    // printf("pushh3 %p %p\n", obs.m_head, obs.m_item );

                    obs.m_head = const_cast<HOLE*>( holeH );
                    obs.m_item = const_cast<HOLE*>( holeI );

                    aCtx->obstacles.insert( obs );
                    dumpObstacles( aCtx->obstacles );

                    collisionsFound = true;
                }
                else
                {
                    return true;
                }
            }
        }
    }

    printf("HCHE\n");

    if( !aHead->Layers().IsMultilayer() && thisNotFlashed )
        return false;

    if( !Layers().IsMultilayer() && otherNotFlashed )
        return false;

    int clearance;

    if( aCtx && aCtx->options.m_overrideClearance >= 0 )
    {
        clearance = aCtx->options.m_overrideClearance;
    }
    else
    {
        clearance = aNode->GetClearance( this, aHead );
    }

    // prevent bogus collisions between the item and its own hole. FIXME: figure out a cleaner way of doing that
    if( holeI && aHead == holeI->ParentPadVia() )
        return false;

    if( holeH && this == holeH->ParentPadVia() )
        return false;

    if( holeH && this == holeH )
        return false;

    if( holeI && aHead == holeI )
        return false;

    if( clearance >= 0 )
    {
        bool checkCastellation = ( m_parent && m_parent->GetLayer() == Edge_Cuts );
        bool checkNetTie = aNode->GetRuleResolver()->IsInNetTie( this );

        if( checkCastellation || checkNetTie )
        {
            // Slow method
            int      actual;
            VECTOR2I pos;

            if( shapeH->Collide( shapeI, clearance + lineWidthH + lineWidthI - clearanceEpsilon,
                                 &actual, &pos ) )
            {
                if( checkCastellation && aNode->QueryEdgeExclusions( pos ) )
                    return false;

                if( checkNetTie && aNode->GetRuleResolver()->IsNetTieExclusion( aHead, pos, this ) )
                    return false;

                if( aCtx )
                {
                    collisionsFound = true;
                    OBSTACLE obs;
                    obs.m_head = const_cast<ITEM*>( aHead );
                    obs.m_item = const_cast<ITEM*>( this );
                    obs.m_clearance = clearance;
                    aCtx->obstacles.insert( obs );
                }
                else
                {
                    return true;
                }
            }
        }
        else
        {
            // Fast method
            if( shapeH->Collide( shapeI, clearance + lineWidthH + lineWidthI - clearanceEpsilon ) )
            {
                if( aCtx )
                {
                    collisionsFound = true;
                    OBSTACLE obs;
                    obs.m_head = const_cast<ITEM*>( aHead );
                    obs.m_item = const_cast<ITEM*>( this );
                    obs.m_clearance = clearance;

                    //printf("i %p h %p ih %p hh %p\n", this ,aHead, holeI, holeH);
                    printf("HCHX %d %d\n", clearance, clearance + lineWidthH + lineWidthI - clearanceEpsilon);
                    //printf("pushc %p %p cl %d cle %d\n", obs.m_head, obs.m_item, clearance, clearance + lineWidthH + lineWidthI - clearanceEpsilon );
                    //printf("SH %s\n", shapeH->Format().c_str(), aHead );
                    //printf("SI %s\n", shapeI->Format().c_str(), this );

                    aCtx->obstacles.insert( obs );

                    dumpObstacles( aCtx->obstacles );
                    printf("--EndDump\n");
                }
                else
                {
                    return true;
                }
            }
        }
    }

    return collisionsFound;
}


bool ITEM::Collide( const ITEM* aOther, const NODE* aNode, COLLISION_SEARCH_CONTEXT *aCtx ) const
{
    if( collideSimple( aOther, aNode, aCtx ) )
        return true;

    // Special cases for "head" lines with vias attached at the end.  Note that this does not
    // support head-line-via to head-line-via collisions, but you can't route two independent
    // tracks at once so it shouldn't come up.

    if( m_kind == LINE_T )
    {
        const LINE* line = static_cast<const LINE*>( this );

        if( line->EndsWithVia() && line->Via().collideSimple( aOther, aNode, aCtx ) )
            return true;
    }

    if( aOther->m_kind == LINE_T )
    {
        const LINE* line = static_cast<const LINE*>( aOther ); // fixme

        if( line->EndsWithVia() && line->Via().collideSimple( this, aNode, aCtx ) )
            return true;
    }

    return false;
}


std::string ITEM::KindStr() const
{
    switch( m_kind )
    {
    case ARC_T:       return "arc";
    case LINE_T:      return "line";
    case SEGMENT_T:   return "segment";
    case VIA_T:       return "via";
    case JOINT_T:     return "joint";
    case SOLID_T:     return "solid";
    case DIFF_PAIR_T: return "diff-pair";
    case HOLE_T:      return "hole";

    default:          return "unknown";
    }
}


ITEM::~ITEM()
{
}

HOLE::~HOLE()
{
    delete m_holeShape;
}

HOLE* HOLE::Clone() const
{
    HOLE* h = new HOLE( m_parentPadVia, m_holeShape->Clone() );

    h->SetNet( Net() );
    h->SetLayers( Layers() );

    h->m_rank = m_rank;
    h->m_marker = m_marker;
    h->m_parent = m_parent;
    h->m_isVirtual = m_isVirtual;

    return h;
}

const SHAPE_LINE_CHAIN HOLE::Hull( int aClearance, int aWalkaroundThickness, int aLayer ) const
{
    if( !m_holeShape )
        return SHAPE_LINE_CHAIN();

    if( m_holeShape->Type() == SH_CIRCLE )
    {
        auto cir = static_cast<SHAPE_CIRCLE*>( m_holeShape );
        int cl = ( aClearance + aWalkaroundThickness / 2 );
        int width = cir->GetRadius() * 2;

        // Chamfer = width * ( 1 - sqrt(2)/2 ) for equilateral octagon
        return OctagonalHull( cir->GetCenter() - VECTOR2I( width / 2, width / 2 ), VECTOR2I( width, width ), cl,
                            ( 2 * cl + width ) * ( 1.0 - M_SQRT1_2 ) );
    }
    else if( m_holeShape->Type() == SH_COMPOUND )
    {
        SHAPE_COMPOUND* cmpnd = static_cast<SHAPE_COMPOUND*>( m_holeShape );

        if ( cmpnd->Shapes().size() == 1 )
        {
            return BuildHullForPrimitiveShape( cmpnd->Shapes()[0], aClearance,
                                               aWalkaroundThickness );
        }
        else
        {
            SHAPE_POLY_SET hullSet;

            for( SHAPE* shape : cmpnd->Shapes() )
            {
                hullSet.AddOutline( BuildHullForPrimitiveShape( shape, aClearance,
                                                                aWalkaroundThickness ) );
            }

            hullSet.Simplify( SHAPE_POLY_SET::PM_STRICTLY_SIMPLE );
            return hullSet.Outline( 0 );
        }
    }
    else
    {
        return BuildHullForPrimitiveShape( m_holeShape, aClearance, aWalkaroundThickness );
    }
}

bool HOLE::IsCircular() const
{
    return m_holeShape->Type() == SH_CIRCLE;
}

int HOLE::Radius() const
{
    assert( m_holeShape->Type() == SH_CIRCLE );

    return static_cast<const SHAPE_CIRCLE*>( m_holeShape )->GetRadius();
}

const VECTOR2I HOLE::Pos() const
{
    return VECTOR2I( 0, 0 ); // fixme holes
}

void HOLE::SetCenter( const VECTOR2I& aCenter )
{
    assert( m_holeShape->Type() == SH_CIRCLE );
    static_cast<SHAPE_CIRCLE*>( m_holeShape )->SetCenter( aCenter );
}
void HOLE::SetRadius( int aRadius )
{
    assert( m_holeShape->Type() == SH_CIRCLE );
    static_cast<SHAPE_CIRCLE*>( m_holeShape )->SetRadius( aRadius );
}


void HOLE::Move( const VECTOR2I& delta )
{
    m_holeShape->Move( delta );
}

HOLE* HOLE::MakeCircularHole( const VECTOR2I& pos, int radius )
{
    auto circle = new SHAPE_CIRCLE( pos, radius );
    auto hole = new HOLE( nullptr, circle );
    hole->SetLayers( LAYER_RANGE( F_Cu, B_Cu ) );
    return hole;
}


const std::string ITEM::Format() const
{
    std::stringstream ss;
    ss << KindStr() << " ";
    ss << "net " << m_net << " ";
    ss << "layers " << m_layers.Start() << " " << m_layers.End();
    return ss.str();
}

}
