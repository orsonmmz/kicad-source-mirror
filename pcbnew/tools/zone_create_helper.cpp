/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 * Copyright (C) 2017 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <tools/zone_create_helper.h>

#include <view/view.h>
#include <tool/tool_manager.h>
#include <class_zone.h>
#include <board_commit.h>
#include <pcb_painter.h>

#include <tools/pcb_actions.h>
#include <tools/selection_tool.h>


ZONE_CREATE_HELPER::ZONE_CREATE_HELPER( DRAWING_TOOL& aTool,
                   const PARAMS& aParams ):
        m_tool( aTool ),
        m_params( aParams ),
        m_parentView( *aTool.getView() )
{
    m_parentView.Add( &m_previewItem );
}


ZONE_CREATE_HELPER::~ZONE_CREATE_HELPER()
{
    // remove the preview from the view
    m_parentView.SetVisible( &m_previewItem, false );
    m_parentView.Remove( &m_previewItem );
}


std::unique_ptr<ZONE_CONTAINER> ZONE_CREATE_HELPER::createNewZone( bool aKeepout )
{
    auto& frame = *m_tool.getEditFrame<PCB_BASE_EDIT_FRAME>();
    auto& board = *m_tool.getModel<BOARD>();

    // Get the current default settings for zones
    ZONE_SETTINGS zoneInfo = frame.GetZoneSettings();
    zoneInfo.m_CurrentZone_Layer = frame.GetScreen()->m_Active_Layer;
    zoneInfo.m_NetcodeSelection = board.GetHighLightNetCode();
    zoneInfo.SetIsKeepout( m_params.m_keepout );

    // Show options dialog
    ZONE_EDIT_T dialogResult;

    if( m_params.m_keepout )
        dialogResult = InvokeKeepoutAreaEditor( &frame, &zoneInfo );
    else
    {
        if( IsCopperLayer( zoneInfo.m_CurrentZone_Layer ) )
            dialogResult = InvokeCopperZonesEditor( &frame, &zoneInfo );
        else
            dialogResult = InvokeNonCopperZonesEditor( &frame, nullptr, &zoneInfo );
    }

    if( dialogResult == ZONE_ABORT )
    {
        return nullptr;
    }

    auto newZone = std::make_unique<ZONE_CONTAINER>( &board );

    // Apply the selected settings
    zoneInfo.ExportSetting( *newZone );

    return newZone;
}


std::unique_ptr<ZONE_CONTAINER> ZONE_CREATE_HELPER::createZoneFromExisting(
        const ZONE_CONTAINER& aSrcZone )
{
    auto& board = *m_tool.getModel<BOARD>();

    auto newZone = std::make_unique<ZONE_CONTAINER>( &board );

    ZONE_SETTINGS zoneSettings;
    zoneSettings << aSrcZone;

    zoneSettings.ExportSetting( *newZone );

    return newZone;
}


void ZONE_CREATE_HELPER::performZoneCutout( ZONE_CONTAINER& aExistingZone,
                                            ZONE_CONTAINER& aCutout )
{
    auto& board = *m_tool.getModel<BOARD>();
    auto& toolMgr = *m_tool.GetManager();

    // Copy cutout corners into existing zone
    for( int ii = 0; ii < aCutout.GetNumCorners(); ii++ )
    {
        aExistingZone.AppendCorner( aCutout.GetCornerPosition( ii ) );
    }

    // Close the current corner list
    aExistingZone.Outline()->CloseLastContour();

    board.OnAreaPolygonModified( nullptr, &aExistingZone );

    // Re-fill if needed
    if( aExistingZone.IsFilled() )
    {
        SELECTION_TOOL* selTool = toolMgr.GetTool<SELECTION_TOOL>();

        auto& selection = selTool->GetSelection();

        selection.Clear();
        selection.Add( &aExistingZone );

        toolMgr.RunAction( PCB_ACTIONS::zoneFill, true );
    }
}


void ZONE_CREATE_HELPER::commitZone( std::unique_ptr<ZONE_CONTAINER> aZone )
{
    auto& frame = *m_tool.getEditFrame<PCB_EDIT_FRAME>();

    if( !m_params.m_keepout )
            frame.Fill_Zone( aZone.get() );

    BOARD_COMMIT bCommit( &m_tool );

    if( m_params.m_mode == DRAWING_TOOL::ZONE_MODE::CUTOUT )
    {
        // For cutouts, subtract from the source
        bCommit.Modify( m_params.m_sourceZone );

        performZoneCutout( *m_params.m_sourceZone, *aZone );

        bCommit.Push( _( "Add a zone cutout" ) );
    }
    else
    {
        // Add the zone as a new board item
        bCommit.Add( aZone.release() );
        bCommit.Push( _( "Add a zone" ) );
    }
};


bool ZONE_CREATE_HELPER::OnFirstPoint()
{
    // if we don't have a zone, create one
    // the user's choice here can affect things like the colour
    // of the preview

    if( !m_zone )
    {
        if( m_params.m_sourceZone )
            m_zone = createZoneFromExisting( *m_params.m_sourceZone );
        else
            m_zone = createNewZone( m_params.m_keepout );

        if( m_zone )
        {
            // set up poperties from zone
            const auto& settings = *m_parentView.GetPainter()->GetSettings();
            COLOR4D color = settings.GetColor( nullptr, m_zone->GetLayer() );

            m_previewItem.SetStrokeColor( color );
            m_previewItem.SetFillColor( color.WithAlpha( 0.2 ) );

            m_parentView.SetVisible( &m_previewItem, true );
        }
    }

    if( !m_zone )
    {
        return false;
    }

    return true;
}


void ZONE_CREATE_HELPER::OnGeometryChange( const POLYGON_GEOM_MANAGER& aMgr )
{
    // send the points to the preview item
    m_previewItem.SetPoints( aMgr.GetLockedInPoints(), aMgr.GetLeaderLinePoints() );
    m_parentView.Update( &m_previewItem, KIGFX::GEOMETRY );
}


void ZONE_CREATE_HELPER::OnComplete( const POLYGON_GEOM_MANAGER& aMgr )
{
    auto& finalPoints = aMgr.GetLockedInPoints();

    if( finalPoints.size() < 3 )
    {
        // just scrap the zone in progress
        m_zone = nullptr;
    }
    else
    {
        m_zone->Outline()->Start( m_zone->GetLayer(),
                                  finalPoints[0].x, finalPoints[0].y,
                                  m_zone->GetHatchStyle() );

        for( size_t i = 1; i < finalPoints.size(); ++i )
        {
            m_zone->AppendCorner( { finalPoints[i].x, finalPoints[i].y } );
        }

        m_zone->Outline()->CloseLastContour();
        m_zone->Outline()->RemoveNullSegments();
        m_zone->Outline()->Hatch();

        // hand the zone over to the committer
        commitZone( std::move( m_zone ) );
    }

    m_parentView.SetVisible( &m_previewItem, false );
}
