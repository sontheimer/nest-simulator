/*
 *  recording_backend_ascii.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Includes from libnestutil:
#include "compose.hpp"

// Includes from nestkernel:
#include "recording_device.h"
#include "vp_manager_impl.h"

// includes from sli:
#include "dictutils.h"

#include "recording_backend_ascii.h"

nest::RecordingBackendASCII::RecordingBackendASCII()
{
}

nest::RecordingBackendASCII::~RecordingBackendASCII() throw()
{
  cleanup();
}

void
nest::RecordingBackendASCII::enroll( const RecordingDevice& device,
  const std::vector< Name >& double_value_names,
  const std::vector< Name >& long_value_names )
{
  const thread t = device.get_thread();
  const index gid = device.get_gid();

  file_map::value_type::iterator file_it = files_[ t ].find( gid );
  if ( file_it != files_[ t ].end() ) // already enrolled
  {
    files_[ t ].erase( file_it );
  }

  std::string filename = build_filename_( device );

  std::ifstream test( filename.c_str() );
  if ( test.good() && not kernel().io_manager.overwrite_files() )
  {
    std::string msg = String::compose(
      "The device file '%1' exists already and will not be overwritten. "
      "Please change data_path, data_prefix or label, or set /overwrite_files "
      "to true in the root node.",
      filename );
    LOG( M_ERROR, "RecordingDevice::calibrate()", msg );

    files_[ t ].insert( std::make_pair( gid, std::make_pair( filename, static_cast< std::ofstream* >( NULL ) ) ) );

    throw IOError();
  }
  test.close();

  std::ofstream* file = new std::ofstream( filename.c_str() );
  ( *file ) << std::fixed << std::setprecision( P_.precision_ );

  if ( not file->good() )
  {
    std::string msg = String::compose(
      "I/O error while opening file '%1'. "
      "This may be caused by too many open files in networks "
      "with many recording devices and threads.",
      filename );
    LOG( M_ERROR, "RecordingDevice::calibrate()", msg );

    files_[ t ].insert( std::make_pair( gid, std::make_pair( filename, static_cast< std::ofstream* >( NULL ) ) ) );

    throw IOError();
  }

  ( *file ) << "# sender";
  if ( device.get_time_in_steps() )
  {
    ( *file ) << "\ttime(step)\toffset";
  }
  else
  {
    ( *file ) << "\ttime(ms)";
  }
  for ( auto& val : double_value_names )
  {
    ( *file ) << "\t" << val;
  }
  for ( auto& val : long_value_names )
  {
    ( *file ) << "\t" << val;
  }
  ( *file ) << std::endl;

  // enroll the device
  files_[ t ].insert( std::make_pair( gid, std::make_pair( filename, file ) ) );
}

void
nest::RecordingBackendASCII::pre_run_hook()
{
  file_map tmp( kernel().vp_manager.get_num_threads() );
  files_.swap( tmp );
}

void
nest::RecordingBackendASCII::post_run_hook()
{
  file_map::iterator inner;
  for ( inner = files_.begin(); inner != files_.end(); ++inner )
  {
    for ( file_map::value_type::iterator f = inner->begin(); f != inner->end(); ++f )
    {
      f->second.second->flush();
    }
  }
}

// JME: Document that Simulate used to append to files previously
// unless close_after_simulate (default:false) was set to true and will
// overwrite files now with nestio.

void
nest::RecordingBackendASCII::cleanup()
{
  file_map::iterator inner;
  for ( inner = files_.begin(); inner != files_.end(); ++inner )
  {
    file_map::value_type::iterator f;
    for ( f = inner->begin(); f != inner->end(); ++f )
    {
      if ( f->second.second != NULL )
      {
        f->second.second->close();
        delete f->second.second;
        f->second.second = NULL;
      }
    }
  }
}

void
nest::RecordingBackendASCII::synchronize()
{
}

void
nest::RecordingBackendASCII::write( const RecordingDevice& device,
  const Event& event,
  const std::vector< double >& double_values,
  const std::vector< long >& long_values )
{
  const thread t = device.get_thread();
  const index gid = device.get_gid();

  if ( files_[ t ].find( gid ) == files_[ t ].end() )
  {
    return;
  }

  const index sender = event.get_sender_gid();
  const Time stamp = event.get_stamp();
  const double offset = event.get_offset();

  std::ofstream& file = *( files_[ t ][ gid ].second );
  file << sender << "\t";
  if ( device.get_time_in_steps() )
  {
    file << stamp.get_steps() << "\t" << offset;
  }
  else
  {
    file << stamp.get_ms() - offset;
  }

  for ( auto& val : double_values )
  {
    file << "\t" << val;
  }
  for ( auto& val : long_values )
  {
    file << "\t" << val;
  }
  file << "\n";
}

const std::string
nest::RecordingBackendASCII::build_filename_( const RecordingDevice& device ) const
{
  // number of digits in number of virtual processes
  const int vpdigits = static_cast< int >(
    std::floor( std::log10( static_cast< float >( kernel().vp_manager.get_num_virtual_processes() ) ) ) + 1 );
  const int giddigits =
    static_cast< int >( std::floor( std::log10( static_cast< float >( kernel().node_manager.size() ) ) ) + 1 );

  std::ostringstream basename;
  const std::string& path = kernel().io_manager.get_data_path();
  if ( not path.empty() )
  {
    basename << path << '/';
  }
  basename << kernel().io_manager.get_data_prefix();

  const std::string& label = device.get_label();
  if ( not label.empty() )
  {
    basename << label;
  }
  else
  {
    basename << device.get_name();
  }

  int vp = device.get_vp();
  int gid = device.get_gid();

  basename << "-" << std::setfill( '0' ) << std::setw( giddigits ) << gid << "-" << std::setfill( '0' )
           << std::setw( vpdigits ) << vp;

  return basename.str() + '.' + P_.file_ext_;
}

void
nest::RecordingBackendASCII::clear( nest::RecordingDevice const& )
{
  // nothing to do
}

void
nest::RecordingBackendASCII::set_device_status( nest::RecordingDevice const&,
  lockPTRDatum< Dictionary, &SLIInterpreter::Dictionarytype > const& )
{
  // nothing to do
}

void
nest::RecordingBackendASCII::prepare()
{
  // nothing to do
}

/* ----------------------------------------------------------------
 * Parameter extraction and manipulation functions
 * ---------------------------------------------------------------- */

nest::RecordingBackendASCII::Parameters_::Parameters_()
  : precision_( 3 )
  , file_ext_( "dat" )
{
}

void
nest::RecordingBackendASCII::Parameters_::get( DictionaryDatum& d ) const
{
  ( *d )[ names::precision ] = precision_;
  ( *d )[ names::file_extension ] = file_ext_;
}

void
nest::RecordingBackendASCII::Parameters_::set( const DictionaryDatum& d )
{
  updateValue< long >( d, names::precision, precision_ );
  updateValue< std::string >( d, names::file_extension, file_ext_ );
}

void
nest::RecordingBackendASCII::set_status( const DictionaryDatum& d )
{
  Parameters_ ptmp = P_; // temporary copy in case of errors
  ptmp.set( d );         // throws if BadProperty

  // if we get here, temporaries contain consistent set of properties
  P_ = ptmp;
}

void
nest::RecordingBackendASCII::get_device_status( const RecordingDevice& device, DictionaryDatum& d ) const
{
  const thread t = device.get_thread();
  const index gid = device.get_gid();

  file_map::value_type::const_iterator file = files_[ t ].find( gid );
  if ( file != files_[ t ].end() )
  {
    initialize_property_array( d, names::filenames );
    append_property( d, names::filenames, file->second.first );
  }
}
