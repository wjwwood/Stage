///////////////////////////////////////////////////////////////////////////
//
// File: model_laser.c
// Author: Richard Vaughan
// Date: 10 June 2004
//
// CVS info:
//  $Source: /home/tcollett/stagecvs/playerstage-cvs/code/stage/src/model_laser.c,v $
//  $Author: rtv $
//  $Revision: 1.46 $
//
///////////////////////////////////////////////////////////////////////////

/** @addtogroup models */
/** @{ */
/** @defgroup model_laser Laser model
 
The laser model simulates a scanning laser rangefinder

@par Worldfile Properties

some properties here

*/
/** @} */

#include <sys/time.h>
#include <math.h>

//#define DEBUG

#include "stage.h"
extern rtk_fig_t* fig_debug;

#define TIMING 0
#define LASER_FILLED 1

  
void laser_init( stg_model_t* mod )
{
  // sensible laser defaults
  stg_geom_t geom;
  geom.pose.x = STG_DEFAULT_LASER_POSEX;
  geom.pose.y = STG_DEFAULT_LASER_POSEY;
  geom.pose.a = STG_DEFAULT_LASER_POSEA;
  geom.size.x = STG_DEFAULT_LASER_SIZEX;
  geom.size.y = STG_DEFAULT_LASER_SIZEY;
  stg_model_set_geom( mod, &geom );

  // set up a laser-specific config structure
  stg_laser_config_t lconf;
  memset(&lconf,0,sizeof(lconf));
  
  lconf.range_min   = STG_DEFAULT_LASER_MINRANGE;
  lconf.range_max   = STG_DEFAULT_LASER_MAXRANGE;
  lconf.fov         = STG_DEFAULT_LASER_FOV;
  lconf.samples     = STG_DEFAULT_LASER_SAMPLES;
  
  stg_color_t col = stg_lookup_color( STG_LASER_GEOM_COLOR ); 
  stg_model_set_color( mod, &col );

  stg_model_set_config( mod, &lconf, sizeof(lconf) );
}


int laser_update( stg_model_t* mod )
{   
  PRINT_DEBUG1( "[%lu] laser update", mod->world->sim_time );
  
  // no work to do if we're unsubscribed
  if( mod->subs < 1 )
    return 0;
    
  stg_laser_config_t* cfg = mod->cfg;
  assert(cfg);
  stg_geom_t* geom = &mod->geom;

  // get the sensor's pose in global coords
  stg_pose_t pz;
  memcpy( &pz, &geom->pose, sizeof(pz) ); 
  stg_model_local_to_global( mod, &pz );

  PRINT_DEBUG3( "laser origin %.2f %.2f %.2f", pz.x, pz.y, pz.a );

  double sample_incr = cfg->fov / (double)cfg->samples;
  
  double bearing = pz.a - cfg->fov/2.0;
  
#if TIMING
  struct timeval tv1, tv2;
  gettimeofday( &tv1, NULL );
#endif
      
  if( fig_debug ) rtk_fig_clear( fig_debug );

  // make a scan buffer (static for speed, so we only have to allocate
  // memory when the number of samples changes).
  static stg_laser_sample_t* scan = 0;
  scan = realloc( scan, sizeof(stg_laser_sample_t) * cfg->samples );
  
  int t;
  // only compute every second sample, for speed
  //for( t=0; t<cfg->samples-1; t+=2 )
  for( t=0; t<cfg->samples; t++ )
    {
      
      itl_t* itl = itl_create( pz.x, pz.y, bearing, 
			       cfg->range_max, 
			       mod->world->matrix, 
			       PointToBearingRange );
      
      bearing += sample_incr;
      
      stg_model_t* hitmod;
      double range = cfg->range_max;
      //stg_laser_return_t hisreturn = LaserVisible;
      
      while( (hitmod = itl_next( itl )) ) 
	{
	  //printf( "model %d %p   hit model %d %p\n",
	  //  mod->id, mod, hitmod->id, hitmod );
	  
	  // Ignore myself, my children, and my ancestors.
	  if( hitmod == mod || stg_model_is_related(mod,hitmod) )
	    continue;
	  
	  // Stop looking when we see something
	  //hisreturn = hitmdmodel_laser_return(hitmod);
	  
	  if( hitmod->laser_return != LaserTransparent) 
	    {
	      range = itl->range;
	      break;
	    }	
	}

      if( range < cfg->range_min )
	range = cfg->range_min;
            
      // record the range in mm
      //scan[t+1].range = 
	scan[t].range = (uint32_t)( range * 1000.0 );
      // if the object is bright, it has a non-zero reflectance
      //scan[t+1].reflectance = 
	scan[t].reflectance = 
	  (hitmod && (hitmod->laser_return >= LaserBright)) ? 1 : 0;

      itl_destroy( itl );
      //printf( "%d ", sample->range );
    }
  
  // new style
  stg_model_set_data( mod, scan, sizeof(stg_laser_sample_t) * cfg->samples );


#if TIMING
  gettimeofday( &tv2, NULL );
  printf( " laser data update time %.6f\n",
	  (tv2.tv_sec + tv2.tv_usec / 1e6) - 
	  (tv1.tv_sec + tv1.tv_usec / 1e6) );	    
#endif
  
  // laser costs some energy
  //model_energy_consume( mod, STG_ENERGY_COST_LASER );

  return 0; //ok
}


void laser_render_data(  stg_model_t* mod )
{
  
  if( mod->gui.data  )
    rtk_fig_clear(mod->gui.data);
  else // create the figure, store it in the model and keep a local pointer
    mod->gui.data = rtk_fig_create( mod->world->win->canvas,
				    NULL, STG_LAYER_LASERDATA );
  
  rtk_fig_t* fig = mod->gui.data;

  
  stg_pose_t pose;
  stg_model_global_pose( mod, &pose );

  rtk_fig_origin( fig, pose.x, pose.y, pose.a );  

  stg_geom_t* geom = &mod->geom;

  assert(mod->cfg);
  assert(mod->cfg_len == sizeof(stg_laser_config_t));
  
  stg_laser_config_t* cfg = mod->cfg;

  size_t len;
  stg_laser_sample_t* samples = (stg_laser_sample_t*)stg_model_get_data( mod, &len );
  
  if( samples == NULL || len < sizeof(stg_laser_sample_t) )
    {
      PRINT_DEBUG( "no laser data available" );
      return;
    }
  
  double sample_incr = cfg->fov / cfg->samples;
  double bearing = geom->pose.a - cfg->fov/2.0;
  stg_point_t* points = calloc( sizeof(stg_point_t), cfg->samples + 1 );
  
  rtk_fig_color_rgb32(fig, stg_lookup_color(STG_LASER_BRIGHT_COLOR) );
  
  int s;
  for( s=0; s<cfg->samples; s++ )
    {
      // useful debug
      //rtk_fig_arrow( fig, 0, 0, bearing, (sample->range/1000.0), 0.01 );
      
      points[1+s].x = (samples[s].range/1000.0) * cos(bearing);
      points[1+s].y = (samples[s].range/1000.0) * sin(bearing);
      bearing += sample_incr;
    }
  
  rtk_fig_color_rgb32(fig, stg_lookup_color(STG_LASER_COLOR) );
  // hmm, what's the right cast to get rid of the compiler warning
  // for the points argument?
  rtk_fig_polygon( fig, 0,0,0, cfg->samples+1, points, 	
		   mod->world->win->fill_polygons );   
  
  rtk_fig_color_rgb32(fig, stg_lookup_color(STG_LASER_BRIGHT_COLOR) );
  
  // loop through again, drawing bright boxes on top of the polygon
  for( s=0; s<cfg->samples; s++ )
    {
      // if this hit point is bright, we draw a little box
      if( samples[s].reflectance > 0 )
	rtk_fig_rectangle( fig, 
			   points[1+s].x, points[1+s].y, 0,
			   0.04, 0.04, 1 );
    }
 
  free( points );
}

void laser_render_config( stg_model_t* mod )
{ 
  PRINT_DEBUG( "laser config render" );
  
  // get the config and make sure it's the right size
  size_t len=0;
  stg_laser_config_t* cfg = (stg_laser_config_t*)stg_model_get_config( mod, &len );
  if( len != sizeof(stg_laser_config_t) )
    {
      PRINT_WARN2( "laser config is wrong size (%d/%d bytes). Not rendering",
		   (int)len, (int)sizeof(stg_laser_config_t) );
      return;
    }


  
  if( mod->gui.cfg  )
    rtk_fig_clear(mod->gui.cfg);
  else // create the figure, store it in the model and keep a local pointer
    mod->gui.cfg = rtk_fig_create( mod->world->win->canvas, 
				   mod->gui.top, STG_LAYER_LASERCONFIG );
  
  rtk_fig_t* fig = mod->gui.cfg;

  
  // draw the FOV and range lines
  rtk_fig_color_rgb32( fig, stg_lookup_color( STG_LASER_CFG_COLOR ));
  
  double mina = cfg->fov / 2.0;
  double maxa = -cfg->fov / 2.0;
  
  double leftfarx = cfg->range_max * cos(mina);
  double leftfary = cfg->range_max * sin(mina);
  double rightfarx = cfg->range_max * cos(maxa);
  double rightfary = cfg->range_max * sin(maxa);
  
  double leftnearx = cfg->range_min * cos(mina);
  double leftneary = cfg->range_min * sin(mina);
  double rightnearx = cfg->range_min * cos(maxa);
  double rightneary = cfg->range_min * sin(maxa);
  
  rtk_fig_line( fig, leftnearx, leftneary, leftfarx, leftfary );
  rtk_fig_line( fig, rightnearx, rightneary, rightfarx, rightfary );
  
  rtk_fig_ellipse_arc( fig,0,0,0, 
		       2.0*cfg->range_max,
		       2.0*cfg->range_max, 
		       mina, maxa );      
  
  rtk_fig_ellipse_arc( fig,0,0,0, 
		       2.0*cfg->range_min,
		       2.0*cfg->range_min, 
		       mina, maxa );      
}

int laser_set_data( stg_model_t* mod, void* data, size_t len )
{
  PRINT_DEBUG( "laser putdata" );
  
  // put the data in the normal way
  _set_data( mod, data, len );
  
    // and render it
  laser_render_data( mod );
}

int laser_set_config( stg_model_t* mod, void* cfg, size_t len )
{
  PRINT_DEBUG( "laser putconfig" );
  
  // put the data in the normal way
  _set_cfg( mod, cfg, len );
  
  // and render it
  laser_render_config( mod );
}

int laser_shutdown( stg_model_t* mod )
{
  // clear the figure
  if( mod->gui.data  ) rtk_fig_clear(mod->gui.data);
  
  return 0; // ok
}


int register_laser( lib_entry_t* lib )
{ 
  assert(lib);
  
  lib[STG_MODEL_LASER].init = laser_init;
  lib[STG_MODEL_LASER].update = laser_update;
  lib[STG_MODEL_LASER].shutdown = laser_shutdown;
  lib[STG_MODEL_LASER].set_config = laser_set_config;
  lib[STG_MODEL_LASER].set_data = laser_set_data;

  return 0; //ok
} 
