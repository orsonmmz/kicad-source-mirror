/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * https://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "sim_plot_panel.h"

#include <algorithm>
#include <limits>


void CURSOR::Plot( wxDC& aDC, mpWindow& aWindow )
{
    if( !m_window )
        m_window = &aWindow;

    if( !m_visible )
        return;

    const auto& dataX = m_trace->GetDataX();
    const auto& dataY = m_trace->GetDataY();

    if( dataX.size() <= 1 )
        return;

    if( m_updateRequired )
    {
        m_coords.x = aWindow.p2x( m_dim.x );

        // Find the closest point coordinates
        auto maxXIt = std::upper_bound( dataX.begin(), dataX.end(), m_coords.x );
        int maxIdx = maxXIt - dataX.begin();
        int minIdx = maxIdx - 1;

        // Out of bounds checks
        if( minIdx < 0 )
        {
            minIdx = 0;
            maxIdx = 1;
            m_coords.x = dataX[0];
        }
        else if( maxIdx >= (int) dataX.size() )
        {
            maxIdx = dataX.size() - 1;
            minIdx = maxIdx - 1;
            m_coords.x = dataX[maxIdx];
        }

        const double leftX = dataX[minIdx];
        const double rightX = dataX[maxIdx];
        const double leftY = dataY[minIdx];
        const double rightY = dataY[maxIdx];

        m_coords.y = leftY + ( rightY - leftY ) / ( rightX - leftX ) * ( m_coords.x - leftX );
        m_updateRequired = false;

        // Notify the parent window about the changes
        wxQueueEvent( aWindow.GetParent(), new wxCommandEvent( EVT_SIM_CURSOR_UPDATE ) );
    }
    else
    {
        UpdateReference();
    }

    // Line length in horizontal and vertical dimensions
    const int horLen = aWindow.GetScrX();
    const int verLen = aWindow.GetScrY();

    const wxPoint cursorPos( aWindow.x2p( m_coords.x ), aWindow.y2p( m_coords.y ) );

    aDC.SetPen( wxPen( *wxBLACK, 1, m_continuous ? wxSOLID : wxLONG_DASH ) );

    aDC.DrawLine( -horLen, cursorPos.y, horLen, cursorPos.y );
    aDC.DrawLine( cursorPos.x, -verLen, cursorPos.x, verLen );
}


SIM_PLOT_PANEL::SIM_PLOT_PANEL( SIM_TYPE aType, wxWindow* parent, wxWindowID id, const wxPoint& pos,
                const wxSize& size, long style, const wxString& name )
    : mpWindow( parent, id, pos, size, style ), m_colorIdx( 0 ),
        m_axis_x( nullptr ), m_axis_y1( nullptr ), m_axis_y2( nullptr ), m_type( aType )
{
    EnableDoubleBuffer( true );
    SetMargins( 10, 10, 10, 10 );
    LimitView( true );

    switch( m_type )
    {
        case ST_AC:
            m_axis_x = new mpScaleX( wxT( "frequency [Hz]" ), mpALIGN_BORDER_BOTTOM );
            m_axis_y1 = new mpScaleY( wxT( "magnitude [V]" ), mpALIGN_BORDER_LEFT );
            m_axis_y2 = new mpScaleY( wxT( "phase [rad]" ), mpALIGN_BORDER_RIGHT );
            break;

        case ST_DC:
            m_axis_x = new mpScaleX( wxT( "voltage [V]" ), mpALIGN_BORDER_BOTTOM );
            m_axis_y1 = new mpScaleY( wxT( "voltage [V]" ), mpALIGN_BORDER_LEFT );
            break;

        case ST_NOISE:
            m_axis_x = new mpScaleX( wxT( "frequency [Hz]" ), mpALIGN_BORDER_BOTTOM );
            m_axis_y1 = new mpScaleY( wxT( "noise [(V or A)^2/Hz]" ), mpALIGN_BORDER_LEFT );
            break;

        case ST_TRANSIENT:
            m_axis_x = new mpScaleX( wxT( "time [s]" ), mpALIGN_BORDER_BOTTOM );
            m_axis_y1 = new mpScaleY( wxT( "voltage [V]" ), mpALIGN_BORDER_LEFT );
            break;

        default:
            // suppress warnings
            break;
    }

    if( m_axis_x )
    {
        m_axis_x->SetTicks( false );
        AddLayer( m_axis_x );
    }

    if( m_axis_y1 )
    {
        m_axis_y1->SetTicks( false );
        AddLayer( m_axis_y1 );
    }

    if( m_axis_y2 )
    {
        m_axis_y2->SetTicks( false );
        AddLayer( m_axis_y2 );
    }

    m_coords = new mpInfoCoords( wxRect( 0, 0, 100, 40 ), wxWHITE_BRUSH );
    AddLayer( m_coords );
    m_topLevel.push_back( m_coords );

    m_legend = new mpInfoLegend( wxRect( 0, 40, 40, 40 ), wxWHITE_BRUSH );
    AddLayer( m_legend );
    m_topLevel.push_back( m_legend );
}


SIM_PLOT_PANEL::~SIM_PLOT_PANEL()
{
    // ~mpWindow destroys all the added layers, so there is no need to destroy m_traces contents
}


bool SIM_PLOT_PANEL::IsPlottable( SIM_TYPE aSimType )
{
    switch( aSimType )
    {
        case ST_AC:
        case ST_DC:
        case ST_TRANSIENT:
            return true;

        default:
            return false;
    }
}


bool SIM_PLOT_PANEL::AddTrace( const wxString& aSpiceName, const wxString& aName, int aPoints,
                                const double* aT, const double* aY, int aFlags )
{
    TRACE* t = NULL;

    // Find previous entry, if there is one
    auto prev = m_traces.find( aName );
    bool addedNewEntry = ( prev == m_traces.end() );

    if( addedNewEntry )
    {
        // New entry
        t = new TRACE( aName, aSpiceName );
        t->SetPen( wxPen( generateColor(), 1, wxSOLID ) );
        m_traces[aName] = t;

        // It is a trick to keep legend & coords always on the top
        for( mpLayer* l : m_topLevel )
            DelLayer( l );

        AddLayer( t );

        for( mpLayer* l : m_topLevel )
            AddLayer( l );
    }
    else
    {
        t = prev->second;
    }

    t->SetData( std::vector<double>( aT, aT + aPoints ), std::vector<double>( aY, aY + aPoints ) );
    UpdateAll();

    return addedNewEntry;
}


bool SIM_PLOT_PANEL::DeleteTrace( const wxString& aName )
{
    auto it = m_traces.find( aName );

    if( it != m_traces.end() )
    {
        m_traces.erase( it );
        TRACE* trace = it->second;

        if( CURSOR* cursor = trace->GetCursor() )
            DelLayer( cursor, true );

        DelLayer( trace, true, true );

        return true;
    }

    return false;
}


void SIM_PLOT_PANEL::DeleteAllTraces()
{
    for( auto& t : m_traces )
    {
        DeleteTrace( t.first );
    }

    m_traces.clear();
}


bool SIM_PLOT_PANEL::HasCursorEnabled( const wxString& aName ) const
{
    TRACE* t = GetTrace( aName );

    return t ? t->HasCursor() : false;
}


void SIM_PLOT_PANEL::EnableCursor( const wxString& aName, bool aEnable )
{
    TRACE* t = GetTrace( aName );

    if( t == nullptr || t->HasCursor() == aEnable )
        return;

    if( aEnable )
    {
        CURSOR* c = new CURSOR( t );
        t->SetCursor( c );
        AddLayer( c );
    }
    else
    {
        CURSOR* c = t->GetCursor();
        t->SetCursor( NULL );
        DelLayer( c, true );
    }

    // Notify the parent window about the changes
    wxQueueEvent( GetParent(), new wxCommandEvent( EVT_SIM_CURSOR_UPDATE ) );
}


wxColour SIM_PLOT_PANEL::generateColor()
{
    /// @todo have a look at:
    /// http://stanford.edu/~mwaskom/software/seaborn/tutorial/color_palettes.html
    /// https://github.com/Gnuplotting/gnuplot-palettes

    const unsigned long colors[] = { 0x000080, 0x008000, 0x800000, 0x008080, 0x800080, 0x808000, 0x808080 };

    //const unsigned long colors[] = { 0xe3cea6, 0xb4781f, 0x8adfb2, 0x2ca033, 0x999afb, 0x1c1ae3, 0x6fbffd, 0x007fff, 0xd6b2ca, 0x9a3d6a };

    // hls
    //const unsigned long colors[] = { 0x0f1689, 0x0f7289, 0x35890f, 0x0f8945, 0x89260f, 0x890f53, 0x89820f, 0x630f89 };

    // pastels, good for dark background
    //const unsigned long colors[] = { 0x2fd8fe, 0x628dfa, 0x53d8a6, 0xa5c266, 0xb3b3b3, 0x94c3e4, 0xca9f8d, 0xac680e };

    const unsigned int colorCount = sizeof(colors) / sizeof(unsigned long);

    /// @todo generate shades to avoid repeating colors
    return wxColour( colors[m_colorIdx++ % colorCount] );
}

wxDEFINE_EVENT( EVT_SIM_CURSOR_UPDATE, wxCommandEvent );
