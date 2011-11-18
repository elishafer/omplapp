/*********************************************************************
* Rice University Software Distribution License
*
* Copyright (c) 2011, Rice University
* All Rights Reserved.
*
* For a full description see the file named LICENSE.
*
*********************************************************************/

/* Author: Ryan Luna */

#ifndef OMPLAPP_GEOMETRY_DETAIL_FCL_METHOD_WRAPPER_
#define OMPLAPP_GEOMETRY_DETAIL_FCL_METHOD_WRAPPER_

#ifdef USE_FCL

// OMPL and OMPL.app headers
#include "omplapp/geometry/GeometrySpecification.h"
#include "omplapp/geometry/detail/assimpUtil.h"

// FCL Headers
#include <fcl/BVH_model.h>
#include <fcl/collision.h>
#include <fcl/collision_node.h>
#include <fcl/transform.h>
#include <fcl/traversal_node_bvhs.h>
#include <fcl/simple_setup.h>
#include <fcl/conservative_advancement.h>

// Boost and STL headers
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <vector>
#include <limits>
#include <cmath>

namespace ob = ompl::base;

namespace ompl
{
    namespace app
    {
        ClassForward (FCLMethodWrapper);

        /// \brief Wrapper for FCL discrete and continuous collision checking and distance queries
        class FCLMethodWrapper
        {
        public:

            typedef boost::function3 <void, fcl::Vec3f&, fcl::SimpleQuaternion&, const base::State*> FCLPoseFromStateCallback;

            FCLMethodWrapper (const GeometrySpecification &geom,
                              const GeometricStateExtractor &se,
                              bool selfCollision,
                              FCLPoseFromStateCallback poseCallback) : extractState_(se), selfCollision_(selfCollision),
                                                                       msg_("FCL Wrapper"), poseFromStateCallback_ (poseCallback)
            {
                configure (geom);
            }

            virtual ~FCLMethodWrapper (void)
            {
            }

            /// \brief Checks whether the given robot state collides with the
            /// environment or itself.
            virtual bool isValid (const base::State *state) const
            {
                bool valid = true;
                boost::mutex::scoped_lock slock(mutex_);
                std::vector <fcl::Contact> contacts;

                if (environment_.num_tris > 0)
                {
                    // Need to adjust robotParts_ for the state configuration.
                    transformRobot (state);

                    // Performing collision checking with environment.
                    for (size_t i = 0; i < robotParts_.size () && valid; ++i)
                    {
                        valid &= fcl::collide (&robotParts_[i], &environment_, 1, false, false, contacts) == 0;
                    }
                }

                // Checking for self collision
                if (selfCollision_ && valid)
                {
                    for (std::size_t i = 0 ; i < robotParts_.size () && valid; ++i)
                    {
                        for (std::size_t j  = i + 1 ; j < robotParts_.size () && valid; ++j)
                        {
                            valid &= fcl::collide (&robotParts_[i], &robotParts_[j], 1, false, false, contacts) == 0;
                        }
                    }
                }

                return valid;
            }

            /// \brief Check the continuous motion between s1 and s2.  If there is a collision
            /// collisionTime will contain the parameterized time to collision in the range [0,1).
            virtual bool isValid (const base::State *s1, const base::State *s2, double &collisionTime) const
            {
                bool valid (true);
                collisionTime = 1.0;

                fcl::SimpleQuaternion quat1, quat2;
                fcl::Vec3f trans1, trans2;
                fcl::Vec3f rot1[3], rot2[3];
                std::vector <fcl::Contact> contacts;

                // Checking for collision with environment
                if (environment_.num_tris > 0)
                {
                    for (size_t i = 0; i < robotParts_.size () && valid; ++i)
                    {
                        // Getting the translation and rotation from s1 and s2
                        poseFromStateCallback_(trans1, quat1, extractState_(s1, i));
                        poseFromStateCallback_(trans2, quat2, extractState_(s2, i));

                        quat1.toRotation (rot1);
                        quat2.toRotation (rot2);

                        // Interpolating part i from s1 to s2
                        fcl::InterpMotion<BVType> motion1 (rot1, trans1, rot2, trans2);
                        // The environment does not move
                        fcl::InterpMotion<BVType> motion2;

                        // Checking for collision
                        valid &= (fcl::conservativeAdvancement <BVType> (&robotParts_[i], &motion1, &environment_, &motion2,
                                                                         1, false, false, contacts, collisionTime) == 0);
                    }
                }

                // Checking for self collision
                if (selfCollision_ && valid)
                {
                    for (std::size_t i = 0 ; i < robotParts_.size () && valid; ++i)
                    {
                        poseFromStateCallback_(trans1, quat1, extractState_(s1, i));
                        poseFromStateCallback_(trans2, quat2, extractState_(s2, i));

                        quat1.toRotation (rot1);
                        quat2.toRotation (rot2);

                        // Interpolating part i from s1 to s2
                        fcl::InterpMotion<BVType> motion_i (rot1, trans1, rot2, trans2);

                        for (std::size_t j = i+1; j < robotParts_.size () && valid; ++j)
                        {
                            poseFromStateCallback_(trans1, quat1, extractState_(s1, j));
                            poseFromStateCallback_(trans2, quat2, extractState_(s2, j));

                            quat1.toRotation (rot1);
                            quat2.toRotation (rot2);

                            // Interpolating part j from s1 to s2
                            fcl::InterpMotion<BVType> motion_j (rot1, trans1, rot2, trans2);

                            // Checking for collision
                            valid &= (fcl::conservativeAdvancement <BVType> (&robotParts_[i], &motion_i, &robotParts_[j], &motion_j,
                                                                             1, false, false, contacts, collisionTime) == 0);
                        }
                    }
                }

                return valid;
            }

            /// \brief Returns the minimum distance from the given robot state and the environment
            virtual double clearance (const base::State *state) const
            {
                double dist = std::numeric_limits<double>::infinity ();

                if (environment_.num_tris > 0)
                {
                    boost::mutex::scoped_lock slock(mutex_);

                    // Need to adjust robotParts_ for the state configuration.
                    transformRobot (state);

                    for (size_t i = 0; i < robotParts_.size (); ++i)
                    {
                        fcl::MeshDistanceTraversalNodeRSS distanceNode;
                        initialize (distanceNode, environment_, robotParts_[i]);

                        // computing minimum distance
                        fcl::distance (&distanceNode);

                        if (distanceNode.min_distance < dist)
                            dist = distanceNode.min_distance;
                    }
                }

                return dist;
            }

         protected:

            /// \brief Transforms (translate and rotate) the components of the
            /// robot to correspond to the given state.
            void transformRobot (const base::State *state) const
            {
                for (size_t i = 0; i < robotParts_.size (); ++i)
                {
                    fcl::SimpleQuaternion quaternion;
                    fcl::Vec3f translation;
                    poseFromStateCallback_(translation, quaternion, extractState_(state, i));

                    robotParts_[i].setTransform (quaternion, translation);
                }
            }

            /// \brief Configures the geometry of the robot and the environment
            /// to setup validity checking.
            void configure (const GeometrySpecification &geom)
            {
                // Configuring the model of the environment
                environment_.beginModel ();
                std::pair <std::vector <fcl::Vec3f>, std::vector<fcl::Triangle> > tri_model;
                tri_model = getFCLModelFromScene (geom.obstacles, geom.obstaclesShift);
                environment_.addSubModel (tri_model.first, tri_model.second);

                environment_.endModel ();
                environment_.computeLocalAABB ();

                if (environment_.num_tris == 0)
                    msg_.inform("Empty environment loaded");
                else
                    msg_.inform("Loaded environment model with %d triangles.", environment_.num_tris);

                // Configuring the model of the robot, composed of one or more pieces
                for (size_t rbt = 0; rbt < geom.robot.size (); ++rbt)
                {
                    Model model;
                    model.beginModel ();
                    aiVector3D shift(0.0, 0.0, 0.0);
                    if (geom.robotShift.size () > rbt)
                        shift = geom.robotShift[rbt];

                    tri_model = getFCLModelFromScene (geom.robot[rbt], shift);
                    model.addSubModel (tri_model.first, tri_model.second);

                    model.endModel ();
                    model.computeLocalAABB ();

                    msg_.inform("Robot piece with %d triangles loaded", model.num_tris);
                    robotParts_.push_back (model);
                }
            }

            /// \brief Convert a mesh to a FCL BVH model
            std::pair <std::vector <fcl::Vec3f>, std::vector<fcl::Triangle> > getFCLModelFromScene (const aiScene *scene, const aiVector3D &center) const
            {
                std::vector<const aiScene*> scenes(1, scene);
                std::vector<aiVector3D>     centers(1, center);
                return getFCLModelFromScene(scenes, centers);
            }

            /// \brief Convert a mesh to a FCL BVH model
            std::pair <std::vector <fcl::Vec3f>, std::vector<fcl::Triangle> >getFCLModelFromScene (const std::vector<const aiScene*> &scenes, const std::vector<aiVector3D> &center) const
            {
                // Model consists of a set of points, and a set of triangles
                // that connect those points
                std::vector<fcl::Triangle> triangles;
                std::vector <fcl::Vec3f> pts;

                for (unsigned int i = 0; i < scenes.size (); ++i)
                {
                    if (scenes[i])
                    {
                        std::vector<aiVector3D> t;
                        // extractTriangles is a misleading name.  this extracts the set of points,
                        // where each set of three contiguous points consists of a triangle
                        scene::extractTriangles (scenes[i], t);

                        if (center.size () > i)
                            for (unsigned int j = 0; j < t.size (); ++j)
                                t[j] -= center[i];

                        assert (t.size () % 3 == 0);

                        for (unsigned int j = 0; j < t.size (); ++j)
                        {
                            pts.push_back (fcl::Vec3f (t[j][0], t[j][1], t[j][2]));
                        }

                        for (unsigned int j = 0; j < t.size (); j+=3)
                            triangles.push_back (fcl::Triangle (j, j+1, j+2));
                    }
                }
                return std::make_pair (pts, triangles);
            }

            /// \brief The type of geometric bounding done for the robot and environment
            typedef fcl::RSS  BVType;
            /// \brief The type geometric model used for the meshes
            typedef fcl::BVHModel <BVType> Model;

            /// \brief Geometric model used for the environment
            Model environment_;

            /// \brief List of components for the geometric model of the robot
            mutable std::vector <Model> robotParts_;

            /// \brief Callback to get the geometric portion of a specific state
            GeometricStateExtractor     extractState_;

            /// \brief Flag indicating whether the geometry is checked for self collisions
            bool                        selfCollision_;

            /// \brief Interface used for reporting errors
            msg::Interface              msg_;

            /// \brief Mutex for thread safety.
            mutable boost::mutex        mutex_;

            /// \brief Callback to extract translation and rotation from a state
            FCLPoseFromStateCallback    poseFromStateCallback_;
        };
    }
}

#endif // USE_FCL

#endif