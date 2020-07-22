// Copyright (c) 2019-2020, Lawrence Livermore National Security, LLC and
// other Serac Project Developers. See the top-level LICENSE file for
// details.
//
// SPDX-License-Identifier: (BSD-3-Clause)

/*!
 *******************************************************************************
 * \file Logger.hpp
 *
 * \brief This file contains the all the necessary functions and macros required
 *        for logging as well as a helper function to exit the program gracefully.
 *******************************************************************************
 */

#ifndef SERAC_LOGGER
#define SERAC_LOGGER

#include "axom/slic.hpp"

#include "mpi.h"
#include "fmt/fmt.hpp"

namespace serac {
  /*!
   *****************************************************************************
   * \brief Exits the program gracefully after cleaning up necessary tasks.
   *
   * This performs finalization work needed by the program such as finalizing MPI
   * and flushing and closing the SLIC logger.
   *
   * \param [in] error True if the program should return an error code
   *****************************************************************************
   */
  void ExitGracefully(bool error=false);

// Logger functionality
namespace logger {
  /*!
   *****************************************************************************
   * \brief Initializes and setups the logger.
   *
   * Setups and tailors the SLIC logger for Serac.  Sets the SLIC loggings streams
   * and tells SLIC how to format the messages.  This function also creates different
   * logging streams if you are running serial, parallel, or parallel with Lumberjack
   * support.
   *
   * \param [in] comm MPI communicator that the logger will use
   *****************************************************************************
   */
  bool Initialize(MPI_Comm comm);

  /*!
   *****************************************************************************
   * \brief Finalizes the logger.
   *
   * Closes and finalizes the SLIC logger.
   *****************************************************************************
   */
  void Finalize();

  /*!
   *****************************************************************************
   * \brief Flushes messages currently held by the logger.
   *
   * If running in parallel, SLIC doesn't output messages immediately.  This flushes
   * all messages currently held by SLIC.  This is a collective operation because
   * messages can be spread across MPI ranks.
   *****************************************************************************
   */
  void Flush();

}  // namespace logger
}  // namespace serac

// Utility SLIC macros

/*!
*****************************************************************************
* \brief Macro that logs given error message only on rank 0.
*****************************************************************************
*/
#define SLIC_ERROR_RANK0(rank,msg)   SLIC_ERROR_IF(rank==0, msg)

/*!
*****************************************************************************
* \brief Macro that logs given warning message only on rank 0.
*****************************************************************************
*/
#define SLIC_WARNING_RANK0(rank,msg) SLIC_WARNING_IF(rank==0, msg)

/*!
*****************************************************************************
* \brief Macro that logs given info message only on rank 0.
*****************************************************************************
*/
#define SLIC_INFO_RANK0(rank,msg)    SLIC_INFO_IF(rank==0, msg)

/*!
*****************************************************************************
* \brief Macro that logs given debug message only on rank 0.
*****************************************************************************
*/
#define SLIC_DEBUG_RANK0(rank,msg)   SLIC_DEBUG_IF(rank==0, msg)

#endif
