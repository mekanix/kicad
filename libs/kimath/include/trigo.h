/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018-2021 KiCad Developers, see AUTHORS.txt for contributors.
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

#ifndef TRIGO_H
#define TRIGO_H

#include <cmath>
#include <math/vector2d.h>
#include <geometry/eda_angle.h>

/**
 * Test if \a aTestPoint is on line defined by \a aSegStart and \a aSegEnd.
 *
 * This function is faster than #TestSegmentHit() because \a aTestPoint  should be exactly on
 * the line.  This works fine only for H, V and 45 degree line segments.
 *
 * @param aSegStart The first point of the line segment.
 * @param aSegEnd The second point of the line segment.
 * @param aTestPoint The point to test.
 *
 * @return true if the point is on the line segment.
 */
bool IsPointOnSegment( const VECTOR2I& aSegStart, const VECTOR2I& aSegEnd,
                       const VECTOR2I& aTestPoint );

/**
 * Test if two lines intersect.
 *
 * @param a_p1_l1 The first point of the first line.
 * @param a_p2_l1 The second point of the first line.
 * @param a_p1_l2 The first point of the second line.
 * @param a_p2_l2 The second point of the second line.
 * @param aIntersectionPoint is filled with the intersection point if it exists
 * @return bool - true if the two segments defined by four points intersect.
 * (i.e. if the 2 segments have at least a common point)
 */
bool SegmentIntersectsSegment( const VECTOR2I& a_p1_l1, const VECTOR2I& a_p2_l1,
                               const VECTOR2I& a_p1_l2, const VECTOR2I& a_p2_l2,
                               VECTOR2I* aIntersectionPoint = nullptr );

/*
 * Calculate the new point of coord coord pX, pY,
 * for a rotation center 0, 0, and angle in (1/10 degree)
 */
void RotatePoint( int *pX, int *pY, double angle );

inline void RotatePoint( int *pX, int *pY, const EDA_ANGLE& angle )
{
    RotatePoint( pX, pY, angle.AsTenthsOfADegree() );
}

/*
 * Calculate the new point of coord coord pX, pY,
 * for a rotation center cx, cy, and angle in (1/10 degree)
 */
void RotatePoint( int *pX, int *pY, int cx, int cy, double angle );

inline void RotatePoint( int *pX, int *pY, int cx, int cy, const EDA_ANGLE& angle )
{
    RotatePoint( pX, pY, cx, cy, angle.AsTenthsOfADegree() );
}

/*
 * Calculate the new coord point point for a rotation angle in (1/10 degree).
 */
inline void RotatePoint( VECTOR2I& point, double angle )
{
    RotatePoint( &point.x, &point.y, angle );
}

inline void RotatePoint( VECTOR2I& point, const EDA_ANGLE& angle )
{
    RotatePoint( &point.x, &point.y, angle.AsTenthsOfADegree() );
}

void RotatePoint( VECTOR2I& point, const VECTOR2I& centre, double angle );

inline void RotatePoint( VECTOR2I& point, const VECTOR2I& centre, const EDA_ANGLE& angle )
{
    RotatePoint( point, centre, angle.AsTenthsOfADegree() );
}

/*
 * Calculate the new coord point point for a center rotation center and angle in (1/10 degree).
 */

void RotatePoint( double* pX, double* pY, double angle );

inline void RotatePoint( double* pX, double* pY, const EDA_ANGLE& angle )
{
    RotatePoint( pX, pY, angle.AsTenthsOfADegree() );
}

inline void RotatePoint( VECTOR2D& point, const EDA_ANGLE& angle )
{
    RotatePoint( &point.x, &point.y, angle.AsTenthsOfADegree() );
}

void RotatePoint( double* pX, double* pY, double cx, double cy, double angle );

inline void RotatePoint( double* pX, double* pY, double cx, double cy, const EDA_ANGLE& angle )
{
    RotatePoint( pX, pY, cx, cy, angle.AsTenthsOfADegree() );
}

inline void RotatePoint( VECTOR2D& point, const VECTOR2D& aCenter, const EDA_ANGLE& angle )
{
    RotatePoint( &point.x, &point.y, aCenter.x, aCenter.y, angle.AsTenthsOfADegree() );
}

/**
 * Determine the center of an arc or circle given three points on its circumference.
 *
 * @param aStart The starting point of the circle (equivalent to aEnd)
 * @param aMid The point on the arc, half-way between aStart and aEnd
 * @param aEnd The ending point of the circle (equivalent to aStart)
 * @return The center of the circle
 */
const VECTOR2I CalcArcCenter( const VECTOR2I& aStart, const VECTOR2I& aMid, const VECTOR2I& aEnd );
const VECTOR2D CalcArcCenter( const VECTOR2D& aStart, const VECTOR2D& aMid, const VECTOR2D& aEnd );
const VECTOR2I CalcArcCenter( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                              const EDA_ANGLE& aAngle );

/**
 * Return the subtended angle for a given arc.
 */
double CalcArcAngle( const VECTOR2I& aStart, const VECTOR2I& aMid, const VECTOR2I& aEnd );

/**
 * Return the middle point of an arc, half-way between aStart and aEnd. There are two possible
 * solutions which can be found by toggling aMinArcAngle. The behaviour is undefined for
 * semicircles (i.e. 180 degree arcs).
 *
 * @param aStart The starting point of the arc (for calculating the radius)
 * @param aEnd The end point of the arc (for determining the arc angle)
 * @param aCenter The center point of the arc
 * @param aMinArcAngle If true, returns the point that results in the smallest arc angle.
 * @return The middle point of the arc
*/
const VECTOR2I CalcArcMid( const VECTOR2I& aStart, const VECTOR2I& aEnd, const VECTOR2I& aCenter,
                           bool aMinArcAngle = true );

/* Return the arc tangent of 0.1 degrees coord vector dx, dy
 * between -1800 and 1800
 * Equivalent to atan2 (but faster for calculations if
 * the angle is 0 to -1800, or + - 900)
 * Lorenzo: In fact usually atan2 already has to do these optimizations
 * (due to the discontinuity in tan) but this function also returns
 * in decidegrees instead of radians, so it's handier
 */
double ArcTangente( int dy, int dx );

inline double EuclideanNorm( const VECTOR2I& vector )
{
    // this is working with doubles
    return hypot( vector.x, vector.y );
}

//! @brief Compute the distance between a line and a reference point
//! Reference: http://mathworld.wolfram.com/Point-LineDistance2-Dimensional.html
//! @param linePointA Point on line
//! @param linePointB Point on line
//! @param referencePoint Reference point
inline double DistanceLinePoint( const VECTOR2I& linePointA, const VECTOR2I& linePointB,
                                 const VECTOR2I& referencePoint )
{
    // Some of the multiple double casts are redundant. However in the previous
    // definition the cast was (implicitly) done too late, just before
    // the division (EuclideanNorm gives a double so from int it would
    // be promoted); that means that the whole expression were
    // vulnerable to overflow during int multiplications
    return fabs( ( static_cast<double>( linePointB.x - linePointA.x ) *
                   static_cast<double>( linePointA.y - referencePoint.y ) -
                   static_cast<double>( linePointA.x  - referencePoint.x ) *
                   static_cast<double>( linePointB.y - linePointA.y) )
            / EuclideanNorm( linePointB - linePointA ) );
}

//! @brief Test, if two points are near each other
//! @param pointA First point
//! @param pointB Second point
//! @param threshold The maximum distance
//! @return True or false
inline bool HitTestPoints( const VECTOR2I& pointA, const VECTOR2I& pointB, double threshold )
{
    VECTOR2I vectorAB = pointB - pointA;

    // Compare the distances squared. The double is needed to avoid
    // overflow during int multiplication
    double sqdistance = (double)vectorAB.x * vectorAB.x + (double)vectorAB.y * vectorAB.y;

    return sqdistance < threshold * threshold;
}

/**
 * Test if \a aRefPoint is with \a aDistance on the line defined by \a aStart and \a aEnd..
 *
 * @param aRefPoint = reference point to test
 * @param aStart is the first end-point of the line segment
 * @param aEnd is the second end-point of the line segment
 * @param aDist = maximum distance for hit
*/
bool TestSegmentHit( const VECTOR2I& aRefPoint, const VECTOR2I& aStart, const VECTOR2I& aEnd,
                     int aDist );

/**
 * Return the length of a line segment defined by \a aPointA and \a aPointB.
 *
 * See also EuclideanNorm and Distance for the single vector or four scalar versions.
 *
 * @return Length of a line (as double)
 */
inline double GetLineLength( const VECTOR2I& aPointA, const VECTOR2I& aPointB )
{
    // Implicitly casted to double
    return hypot( aPointA.x - aPointB.x, aPointA.y - aPointB.y );
}

// These are the usual degrees <-> radians conversion routines
inline double DEG2RAD( double deg ) { return deg * M_PI / 180.0; }
inline double RAD2DEG( double rad ) { return rad * 180.0 / M_PI; }

// These are the same *but* work with the internal 'decidegrees' unit
inline double DECIDEG2RAD( double deg ) { return deg * M_PI / 1800.0; }
inline double RAD2DECIDEG( double rad ) { return rad * 1800.0 / M_PI; }

/* These are templated over T (and not simply double) because Eeschema
   is still using int for angles in some place */

/// Normalize angle to be in the 0.0 .. 360.0 range: angle is in 1/10 degrees.
template <class T> inline T NormalizeAnglePos( T Angle )
{
    while( Angle < 0 )
        Angle += 3600;
    while( Angle >= 3600 )
        Angle -= 3600;
    return Angle;
}

template <class T> inline void NORMALIZE_ANGLE_POS( T& Angle )
{
    Angle = NormalizeAnglePos( Angle );
}


/// Normalize angle to be in the -180.0 .. 180.0 range
template <class T> inline T NormalizeAngle180( T Angle )
{
    while( Angle <= -1800 )
        Angle += 3600;

    while( Angle > 1800 )
        Angle -= 3600;

    return Angle;
}

/**
 * Test if an arc from \a aStartAngle to \a aEndAngle crosses the positive X axis (0 degrees).
 *
 * Testing is performed in the quadrant 1 to quadrant 4 direction (counter-clockwise).
 *
 * @param aStartAngle The arc start angle in degrees.
 * @param aEndAngle The arc end angle in degrees.
 */
inline bool InterceptsPositiveX( double aStartAngle, double aEndAngle )
{
    double end = aEndAngle;

    if( aStartAngle > aEndAngle )
        end += 360.0;

    return aStartAngle < 360.0 && end > 360.0;
}

/**
 * Test if an arc from \a aStartAngle to \a aEndAngle crosses the negative X axis (180 degrees).
 *
 * Testing is performed in the quadrant 1 to quadrant 4 direction (counter-clockwise).
 *
 * @param aStartAngle The arc start angle in degrees.
 * @param aEndAngle The arc end angle in degrees.
 */
inline bool InterceptsNegativeX( double aStartAngle, double aEndAngle )
{
    double end = aEndAngle;

    if( aStartAngle > aEndAngle )
        end += 360.0;

    return aStartAngle < 180.0 && end > 180.0;
}

#endif
