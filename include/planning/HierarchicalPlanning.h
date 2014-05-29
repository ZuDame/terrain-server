#ifndef DWL_HierarchicalPlanning_H
#define DWL_HierarchicalPlanning_H

#include <planning/PlanningOfMotionSequences.h>
#include <environment/RewardMap.h>


namespace dwl
{

namespace planning
{

/**
 * @class HierarchicalPlanning
 * @brief Class for solving the problem of planning of motion sequences using a hierarchical approach
 */
class HierarchicalPlanning : public dwl::planning::PlanningOfMotionSequences
{
	public:
		/** @brief Constructor function */
		HierarchicalPlanning();

		/** @brief Destructor function */
		~HierarchicalPlanning();

		/**
		 * @brief Initializes of the hierarchical planning
		 * @return bool Return true if was initialized the hierarchical planning
		 */
		virtual bool init();

		/**
		 * @brief Updates the start and goal body pose for hierarchical planning
		 * @param dwl::planning::BodyPose start Start pose
		 * @param dwl::planning::BodyPose goal Goal pose
		 */
		virtual void update(BodyPose start, BodyPose goal);

		/**
		 * @brief Computes a whole-body motion provided for the hierarchical planning
		 * @return bool Return true if it was computed the plan
		 */
		bool compute();



		//TODO: I think that for Hierarchical Planner we can implement a method for setting the reward map


	private:
		/** @brief Trajectory of body */
		std::vector<Eigen::Vector3d> body_path_;

		/** @brief The id of the start vertex */
		Vertex source_id_;

		/** @brief The id of the goal vertex */
		Vertex target_id_;


}; //@class HierarchicalPlanning

} //@namespace planning

} //@namespace dwl


#endif
