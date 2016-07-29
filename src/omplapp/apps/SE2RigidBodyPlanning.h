/*********************************************************************
* Rice University Software Distribution License
*
* Copyright (c) 2010, Rice University
* All Rights Reserved.
*
* For a full description see the file named LICENSE.
*
*********************************************************************/

/* Author: Ioan Sucan */

#ifndef OMPLAPP_SE2_RIGID_BODY_PLANNING_
#define OMPLAPP_SE2_RIGID_BODY_PLANNING_

#include "omplapp/apps/AppBase.h"
#include <ompl/base/spaces/SE2StateSpace.h>

namespace ompl
{
    namespace app
    {

        /** \brief Wrapper for ompl::app::RigidBodyPlanning that plans
            for rigid bodies in SE2. */
        class SE2RigidBodyPlanning : public AppBase<GEOMETRIC>
        {
        public:

            SE2RigidBodyPlanning() : AppBase<GEOMETRIC>(base::StateSpacePtr(new base::SE2StateSpace()), Motion_2D)
            {
                name_ = "Rigid body planning (2D)";
            }

            virtual ~SE2RigidBodyPlanning() = default;

            bool isSelfCollisionEnabled() const
            {
                return false;
            }

            virtual base::ScopedState<> getDefaultStartState() const;

            virtual base::ScopedState<> getFullStateFromGeometricComponent(const base::ScopedState<> &state) const
            {
                return state;
            }

            virtual const base::StateSpacePtr& getGeometricComponentStateSpace() const
            {
                return getStateSpace();
            }

            virtual unsigned int getRobotCount() const
            {
                return 1;
            }

        protected:

            virtual const base::State* getGeometricComponentStateInternal(const base::State* state, unsigned int /*index*/) const
            {
                return state;
            }

        };

    }
}

#endif
