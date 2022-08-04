// -*-c++-*-

/*!
  \file intercept_table.cpp
  \brief interception info holder Source File
*/

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 3 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "intercept_table.h"
#include "self_intercept_simulator.h"
#include "player_intercept.h"
#include "world_model.h"
#include "player_object.h"
#include "abstract_player_object.h"

#include <rcsc/time/timer.h>
#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/game_time.h>

#include <algorithm>
#include <limits>

// #define DEBUG_PRINT

namespace rcsc {

namespace {
const int MAX_STEP = 50;
}

const double InterceptInfo::MIN_VALUE = -std::numeric_limits< double >::max();

/*-------------------------------------------------------------------*/
/*!

*/
InterceptTable::InterceptTable()
    : M_update_time( 0, 0 )
{
    M_ball_cache.reserve( MAX_STEP );
    M_self_cache.reserve( ( MAX_STEP + 1 ) * 2 );

    clear();
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::clear()
{
    M_ball_cache.clear();

    M_self_step = 1000;
    M_self_exhaust_step = 1000;
    M_teammate_step = 1000;
    M_second_teammate_step = 1000;
    M_goalie_step = 1000;
    M_opponent_step = 1000;
    M_second_opponent_step = 1000;

    M_first_teammate = nullptr;
    M_second_teammate = nullptr;
    M_first_opponent = nullptr;
    M_second_opponent = nullptr;

    M_self_cache.clear();

    M_player_map.clear();
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::update( const WorldModel & wm )
{
    if ( wm.time() == M_update_time )
    {
        return;
    }
    M_update_time = wm.time();

#ifdef DEBUG_PRINT
    dlog.addText( Logger::INTERCEPT,
                  __FILE__" (update)" );
    Timer timer;
#endif

    // clear all data
    this->clear();

    // playmode check
    if ( wm.gameMode().type() == GameMode::TimeOver
         || wm.gameMode().type() == GameMode::BeforeKickOff )
    {
        return;
    }

    if ( ! wm.self().posValid()
         || ! wm.ball().posValid() )
    {
        dlog.addText( Logger::INTERCEPT,
                      __FILE__" (update) Invalid self or ball pos" );
        return;
    }

#ifdef DEBUG
    if ( wm.self().isKickable()
         || wm.kickableTeammate()
         || wm.kickableOpponent() )
    {
        dlog.addText( Logger::INTERCEPT,
                      __FILE__" (update) Already exist kickable player" );
    }
#endif

    createBallCache( wm );

#ifdef DEBUG
    dlog.addText( Logger::INTERCEPT,
                  "==========Intercept Predict Self==========" );
#endif

    predictSelf( wm );

#ifdef DEBUG
    dlog.addText( Logger::INTERCEPT,
                  "==========Intercept Predict Opponent==========" );
#endif

    predictOpponent( wm );

#ifdef DEBUG
    dlog.addText( Logger::INTERCEPT,
                  "==========Intercept Predict Teammate==========" );
#endif

    predictTeammate( wm );

    dlog.addText( Logger::INTERCEPT,
                  "<-----Intercept Self reach step = %d. exhaust reach step = %d ",
                  M_self_step,
                  M_self_exhaust_step );
    if ( M_first_teammate )
    {
        dlog.addText( Logger::INTERCEPT,
                      "<-----Intercept Teammate  fastest reach step = %d."
                      " teammate %d (%.1f %.1f)",
                      M_teammate_step,
                      M_first_teammate->unum(),
                      M_first_teammate->pos().x,
                      M_first_teammate->pos().y );

    }

    if ( M_second_teammate )
    {
        dlog.addText( Logger::INTERCEPT,
                      "<-----Intercept Teammate  2nd     reach step = %d."
                      " teammate %d (%.1f %.1f)",
                      M_second_teammate_step,
                      M_second_teammate->unum(),
                      M_second_teammate->pos().x,
                      M_second_teammate->pos().y );
    }

    if ( M_first_opponent )
    {
        dlog.addText( Logger::INTERCEPT,
                      "<-----Intercept Opponent  fastest reach step = %d."
                      " opponent %d (%.1f %.1f)",
                      M_opponent_step,
                      M_first_opponent->unum(),
                      M_first_opponent->pos().x,
                      M_first_opponent->pos().y );
    }

    if ( M_second_opponent )
    {
        dlog.addText( Logger::INTERCEPT,
                      "<-----Intercept Opponent  2nd     reach step = %d."
                      " opponent %d (%.1f %.1f)",
                      M_second_opponent_step,
                      M_second_opponent->unum(),
                      M_second_opponent->pos().x,
                      M_second_opponent->pos().y );
    }

#ifdef DEBUG_PRINT
    dlog.addText( Logger::INTERCEPT,
                  __FILE__":(update) elapsed %.3f [ms]", timer.elapsedReal() );
#endif
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::hearTeammate( const WorldModel & wm,
                              const int unum,
                              const int step )
{
    if ( M_first_teammate
         && step >= M_teammate_step )
    {
        return;
    }

    const PlayerObject * target = nullptr;
    for ( const PlayerObject * t : wm.teammates() )
    {
        if ( t->unum() == unum )
        {
            target = t;
            break;
        }
    }

    if ( target )
    {
        M_first_teammate = target;
        M_teammate_step = step;

        M_player_map[ target ] = step;

        dlog.addText( Logger::INTERCEPT,
                      "<----- Hear Intercept Teammate  fastest reach step = %d."
                      " teammate %d (%.1f %.1f)",
                      M_teammate_step,
                      M_first_teammate->unum(),
                      M_first_teammate->pos().x,
                      M_first_teammate->pos().y );
    }
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::hearOpponent( const WorldModel & wm,
                              const int unum,
                              const int step )
{
    if ( M_first_opponent )
    {
        if ( step >= M_opponent_step )
        {
            dlog.addText( Logger::INTERCEPT,
                          "<----- Hear Intercept Opponent. no update."
                          " exist faster reach step %d >= %d",
                          step, M_opponent_step );
            return;
        }

        if ( M_first_opponent->unum() == unum
             && M_first_opponent->posCount() == 0 )
        {
            dlog.addText( Logger::INTERCEPT,
                          "<----- Hear Intercept Opponent . no update."
                          " opponent %d (%.1f %.1f) is seen",
                          M_first_opponent->unum(),
                          M_first_opponent->pos().x,
                          M_first_opponent->pos().y );
            return;
        }
    }

    const PlayerObject * p = nullptr;

    for ( const PlayerObject * i : wm.opponents() )
    {
        if ( i->unum() == unum )
        {
            p = i;
            break;
        }
    }


    if ( p )
    {
        M_first_opponent = p;
        M_opponent_step = step;

        M_player_map[ p ] = step;

        dlog.addText( Logger::INTERCEPT,
                      "<----- Hear Intercept Opponent  fastest reach step = %d."
                      " opponent %d (%.1f %.1f)",
                      M_opponent_step,
                      M_first_opponent->unum(),
                      M_first_opponent->pos().x,
                      M_first_opponent->pos().y );
    }
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::createBallCache( const WorldModel & wm )
{
    const ServerParam & SP = ServerParam::i();
    const double max_x = ( SP.keepawayMode()
                           ? SP.keepawayLength() * 0.5
                           : SP.pitchHalfLength() + 5.0 );
    const double max_y = ( SP.keepawayMode()
                           ? SP.keepawayWidth() * 0.5
                           : SP.pitchHalfWidth() + 5.0 );
    const double bdecay = SP.ballDecay();

    Vector2D bpos = wm.ball().pos();
    Vector2D bvel = ( wm.kickableOpponent()
                      //|| wm.kickableTeammate()
                      )
        ? Vector2D( 0.0, 0.0 )
        : wm.ball().vel();
    double bspeed = bvel.r();

    for ( int i = 0; i < MAX_STEP; ++i )
    {
        M_ball_cache.push_back( bpos );

        if ( bspeed < 0.005 && i >= 10 )
        {
            break;
        }

        bpos += bvel;
        bvel *= bdecay;
        bspeed *= bdecay;

        if ( max_x < bpos.absX()
             || max_y < bpos.absY() )
        {
            break;
        }
    }

#ifdef DEBUG_PRINT
    dlog.addText( Logger::INTERCEPT,
                  "(InterceptTable::createBallCache) size=%d last pos=(%.2f %.2f)",
                  M_ball_cache.size(),
                  M_ball_cache.back().x, M_ball_cache.back().y );
#endif
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::predictSelf( const WorldModel & wm )
{
    if ( wm.self().isKickable() )
    {
        dlog.addText( Logger::INTERCEPT,
                      "Intercept Self. already kickable. no estimation loop!" );
        M_self_step = 0;
        M_self_exhaust_step = 0;
        return;
    }

    int max_step = std::min( MAX_STEP, static_cast< int >( M_ball_cache.size() ) );

    // SelfInterceptV13 predictor( wm );
    // predictor.predict( max_step, M_self_cache );
    SelfInterceptSimulator sim;
    sim.simulate( wm, max_step, M_self_cache );

    if ( M_self_cache.empty() )
    {
        std::cerr << wm.self().unum() << ' '
                  << wm.time()
                  << " Interecet Self cache is empty!"
                  << std::endl;
        dlog.addText( Logger::INTERCEPT,
                      "Intercept Self. Self cache is empty!" );
        // if self cache is empty,
        // reach point should be the inertia final point of the ball
        return;
    }

// #ifdef SELF_INTERCEPT_USE_NO_SAVE_RECEVERY
//     std::sort( M_self_cache.begin(),
//                M_self_cache.end(),
//                InterceptInfo::Cmp() );
//     M_self_cache.erase( std::unique( M_self_cache.begin(),
//                                      M_self_cache.end(),
//                                      InterceptInfo::Equal() ),
//                         M_self_cache.end() );
// #endif

    int min_step = M_self_step;
    int exhaust_min_step = M_self_exhaust_step;

    for ( const InterceptInfo & i : M_self_cache )
    {
        if ( i.staminaType() == InterceptInfo::NORMAL )
        {
            if ( i.reachStep() < min_step )
            {
                min_step = i.reachStep();
            }
        }
        else if ( i.staminaType() == InterceptInfo::EXHAUST )
        {
            if ( i.reachStep() < exhaust_min_step )
            {
                exhaust_min_step = i.reachStep();
            }
        }
    }

    dlog.addText( Logger::INTERCEPT,
                  "Intercept Self. solution size = %d",
                  M_self_cache.size() );

    M_self_step = min_step;
    M_self_exhaust_step = exhaust_min_step;

    //M_player_map.insert( std::pair< const AbstractPlayerObject *, int >( &(wm.self()), min_step ) );
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::predictTeammate( const WorldModel & wm )
{
    int min_step = 1000;
    int second_min_step = 1000;

    if ( wm.kickableTeammate() )
    {
        M_teammate_step = 0;
        min_step = 0;
        M_first_teammate = wm.kickableTeammate();

        dlog.addText( Logger::INTERCEPT,
                      "Intercept Teammate. exist kickable teammate" );
        dlog.addText( Logger::INTERCEPT,
                      "---> set fastest teammate %d (%.1f %.1f)",
                      M_first_teammate->unum(),
                      M_first_teammate->pos().x, M_first_teammate->pos().y );
    }

    PlayerIntercept predictor( wm, M_ball_cache );

    for ( const PlayerObject * t : wm.teammatesFromBall() )
    {
        if ( t == wm.kickableTeammate() )
        {
            M_player_map[ t ] = 0;
            continue;
        }

        if ( t->posCount() >= 10 )
        {
            dlog.addText( Logger::INTERCEPT,
                          "Intercept Teammate %d.(%.1f %.1f) Low accuracy %d. skip...",
                          t->unum(),
                          t->pos().x, t->pos().y,
                          t->posCount() );
            continue;
        }

        int step = predictor.predict( *t, false );
        int goalie_step = 1000;
        if ( t->goalie() )
        {
            goalie_step = predictor.predict( *t, true );
            if ( step > goalie_step )
            {
                step = goalie_step;
            }
        }

        dlog.addText( Logger::INTERCEPT,
                      "---> Teammate %d.(%.1f %.1f) step=%d",
                      t->unum(),
                      t->pos().x, t->pos().y,
                      step );

        if ( t->goalie() )
        {
            M_goalie_step = goalie_step;
        }

        if ( step < second_min_step )
        {
            second_min_step = step;
            M_second_teammate = t;

            if ( second_min_step < min_step )
            {
                std::swap( min_step, second_min_step );
                std::swap( M_first_teammate, M_second_teammate );
            }
        }

        M_player_map[ t ] = step;
    }

    if ( M_second_teammate && second_min_step < 1000 )
    {
        M_second_teammate_step = second_min_step;
    }

    if ( M_first_teammate && min_step < 1000 )
    {
        M_teammate_step = min_step;
    }
}

/*-------------------------------------------------------------------*/
/*!

*/
void
InterceptTable::predictOpponent( const WorldModel & wm )
{
    int min_step = 1000;
    int second_min_step = 1000;

    if ( wm.kickableOpponent() )
    {
        M_opponent_step = 0;
        min_step = 0;
        M_first_opponent = wm.kickableOpponent();

        dlog.addText( Logger::INTERCEPT,
                      "Intercept Opponent. exist kickable opponent" );
        dlog.addText( Logger::INTERCEPT,
                      "---> set fastest opponent %d (%.1f %.1f)",
                      M_first_opponent->unum(),
                      M_first_opponent->pos().x, M_first_opponent->pos().y );
    }

    PlayerIntercept predictor( wm, M_ball_cache );

    for ( const PlayerObject * o : wm.opponentsFromBall() )
    {
        if ( o == wm.kickableOpponent() )
        {
            M_player_map[ o ] = 0;
            continue;
        }

        if ( o->posCount() >= 15 )
        {
            dlog.addText( Logger::INTERCEPT,
                          "Intercept Opponent %d.(%.1f %.1f) Low accuracy %d. skip...",
                          o->unum(),
                          o->pos().x, o->pos().y,
                          o->posCount() );
            continue;
        }

        int step = predictor.predict( *o, false );
        if ( o->goalie() )
        {
            int goalie_step = predictor.predict( *o, true );
            if ( goalie_step > 0
                 && step > goalie_step )
            {
                step = goalie_step;
            }
        }

        dlog.addText( Logger::INTERCEPT,
                      "---> Opponent.%d (%.1f %.1f) step=%d",
                      o->unum(),
                      o->pos().x, o->pos().y,
                      step );

        if ( step < second_min_step )
        {
            second_min_step = step;
            M_second_opponent = o;

            if ( second_min_step < min_step )
            {
                std::swap( min_step, second_min_step );
                std::swap( M_first_opponent, M_second_opponent );
            }
        }

        M_player_map[ o ] = step;
    }

    if ( M_second_opponent && second_min_step < 1000 )
    {
        M_second_opponent_step = second_min_step;
    }

    if ( M_first_opponent && min_step < 1000 )
    {
        M_opponent_step = min_step;
    }
}

}
