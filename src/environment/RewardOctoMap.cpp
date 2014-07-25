#include <environment/RewardOctoMap.h>


namespace dwl
{

namespace environment
{


RewardOctoMap::RewardOctoMap() : is_first_computation_(true), using_cloud_mean_(false)
{
	// Default neighboring area
	setNeighboringArea(-1, 1, -1, 1, -1, 1);
}


RewardOctoMap::~RewardOctoMap()
{

}


void RewardOctoMap::compute(TerrainModel model, Eigen::Vector4d robot_state)
{
	octomap::OcTree* octomap = model.octomap;

	if (!is_added_search_area_) {
		printf(YELLOW "Warning: adding a default search area" COLOR_RESET);
		// Adding a default search area
		addSearchArea(0.5, 3.0, -0.75, 0.75, -0.77, -0.4, 0.04);

		is_added_search_area_ = true;
	}

	double yaw = robot_state(3);

	// Computing reward map for several search areas
	for (int n = 0; n < search_areas_.size(); n++) {
		// Computing the boundary of the gridmap
		Eigen::Vector2d boundary_min, boundary_max;

		boundary_min(0) = search_areas_[n].min_x + robot_state(0);
		boundary_min(1) = search_areas_[n].min_y + robot_state(1);
		boundary_max(0) = search_areas_[n].max_x + robot_state(0);
		boundary_max(1) = search_areas_[n].max_y + robot_state(1);

		for (double y = boundary_min(1); y < boundary_max(1); y += search_areas_[n].grid_resolution) {
			for (double x = boundary_min(0); x < boundary_max(0); x += search_areas_[n].grid_resolution) {
				// Computing the rotated coordinate of the point inside the search area
				double xr = (x - robot_state(0)) * cos(yaw) - (y - robot_state(1)) * sin(yaw) + robot_state(0);
				double yr = (x - robot_state(0)) * sin(yaw) + (y - robot_state(1)) * cos(yaw) + robot_state(1);

				// Checking if the cell belongs to dimensions of the map, and also getting the key of this cell
				double z = search_areas_[n].max_z + robot_state(2);
				octomap::OcTreeKey init_key;
				if (!octomap->coordToKeyChecked(xr, yr, z, 16, init_key)) {
					printf(RED "Cell out of bounds\n" COLOR_RESET);

					return;
				}

				// Finding the cell of the surface
				int r = 0;
				while (z >= search_areas_[n].min_z + robot_state(2)) {
					octomap::OcTreeKey heightmap_key;
					octomap::OcTreeNode* heightmap_node = octomap->search(init_key);
					heightmap_key[0] = init_key[0];
					heightmap_key[1] = init_key[1];
					heightmap_key[2] = init_key[2] - r;

					heightmap_node = octomap->search(heightmap_key);
					octomap::point3d height_point = octomap->keyToCoord(heightmap_key);
					z = height_point(2);
					if (heightmap_node) {
						// Computation of the heightmap
						if (octomap->isNodeOccupied(heightmap_node)) {
							// Getting position of the occupied cell
							Key cell_key;
							Eigen::Vector3d cell_position;
							cell_position(0) = height_point(0);
							cell_position(1) = height_point(1);
							cell_position(2) = height_point(2);
							getCell(cell_key, cell_position);

							Vertex vertex_id = gridmap_.keyToVertex(cell_key);
							if (is_first_computation_)
								addCellToTerrainHeightMap(cell_key);
							else {
								bool new_status = true;
								if ((reward_gridmap_.find(vertex_id)->first == vertex_id)) {
									// Evaluating if it changed status (height)
									Cell reward_cell = reward_gridmap_.find(vertex_id)->second;
									if (reward_cell.key.z != cell_key.z) {
										removeCellToRewardMap(reward_cell.key);
										removeCellToTerrainHeightMap(reward_cell.key);
									} else
										new_status = false;
								}

								if (new_status)
									addCellToTerrainHeightMap(cell_key);
							}
							break;
						}
					}
					r++;
				}
			}
		}
	}
	is_first_computation_ = false;


	// Computing the total reward
	std::map<Vertex, double> terrain_heightmap = terrain_heightmap_;
	for (std::map<Vertex, double>::iterator terrain_iter = terrain_heightmap.begin();
			terrain_iter != terrain_heightmap.end();
			terrain_iter++)
	{
		octomap::OcTreeKey heightmap_key;
		Vertex v = terrain_iter->first;
		Eigen::Vector3d xy_coord = gridmap_.vertexToCoord(v);//TODO
		double height = terrain_iter->second;
		octomap::point3d terrain_point;
		terrain_point(0) = xy_coord(0);
		terrain_point(1) = xy_coord(1);
		terrain_point(2) = height;
		heightmap_key = octomap->coordToKey(terrain_point);

		// Converting the key to a vertex id
		Vertex vertex_id = terrain_iter->first;
		if (is_first_computation_)
			computeRewards(octomap, heightmap_key);
		else {
			bool new_status = true;
			if (reward_gridmap_.find(vertex_id)->first == vertex_id) {
				// Evaluating if it's changed status (height)
				Cell reward_cell = reward_gridmap_.find(vertex_id)->second;
				if (reward_cell.key.z != heightmap_key[2]) {
					removeCellToRewardMap(reward_cell.key);
					removeCellToTerrainHeightMap(reward_cell.key);
				} else
					new_status = false;
			}

			if (new_status)
				computeRewards(octomap, heightmap_key);
		}
	}

	// Removing the points that doesn't belong to interest area
	Eigen::Vector3d robot_2dpose;
	robot_2dpose(0) = robot_state(0);
	robot_2dpose(1) = robot_state(1);
	robot_2dpose(2) = robot_state(3);
	removeRewardOutsideInterestRegion(robot_2dpose);
}


void RewardOctoMap::computeRewards(octomap::OcTree* octomap, octomap::OcTreeKey heightmap_key)
{
	Terrain terrain_info;

	std::vector<Eigen::Vector3f> neighbors_position;
	octomap::OcTreeNode* heightmap_node = octomap->search(heightmap_key);

	// Adding to the cloud the point of interest
	Eigen::Vector3f heightmap_position;
	octomap::point3d heightmap_point = octomap->keyToCoord(heightmap_key);
	heightmap_position(0) = heightmap_point(0);
	heightmap_position(1) = heightmap_point(1);
	heightmap_position(2) = heightmap_point(2);
	neighbors_position.push_back(heightmap_position);

	// Iterates over the 8 neighboring sets
	octomap::OcTreeKey neighbor_key;
	octomap::OcTreeNode* neighbor_node = heightmap_node;
	bool is_there_neighboring = false;
	for (int i = neighboring_area_.min_z; i < neighboring_area_.max_z + 1; i++) {
		for (int j = neighboring_area_.min_y; j < neighboring_area_.max_y + 1; j++) {
			for (int k = neighboring_area_.min_x; k < neighboring_area_.max_x + 1; k++) {
				neighbor_key[0] = heightmap_key[0] + k;
				neighbor_key[1] = heightmap_key[1] + j;
				neighbor_key[2] = heightmap_key[2] + i;
				neighbor_node = octomap->search(neighbor_key);

				if (neighbor_node) {
					Eigen::Vector3f neighbor_position;
					octomap::point3d neighbor_point;
					if (octomap->isNodeOccupied(neighbor_node)) {
						neighbor_point = octomap->keyToCoord(neighbor_key);
						neighbor_position(0) = neighbor_point(0);
						neighbor_position(1) = neighbor_point(1);
						neighbor_position(2) = neighbor_point(2);
						neighbors_position.push_back(neighbor_position);

						is_there_neighboring = true;
					}
				}
			}
		}
	}

	if (is_there_neighboring) {
		// Computing terrain info
		EIGEN_ALIGN16 Eigen::Matrix3d covariance_matrix;
		if (neighbors_position.size() < 3 || math_.computeMeanAndCovarianceMatrix(neighbors_position, covariance_matrix, terrain_info.position) == 0)
			return;

			if (!using_cloud_mean_) {
				terrain_info.position(0) = neighbors_position[0](0);
				terrain_info.position(1) = neighbors_position[0](1);
				terrain_info.position(2) = neighbors_position[0](2);
			}

			math_.solvePlaneParameters(covariance_matrix, terrain_info.surface_normal, terrain_info.curvature);
	}

	terrain_info.height_map = terrain_heightmap_;
	terrain_info.gridmap_resolution = gridmap_.getEnvironmentResolution();

	// Computing the reward
	if (is_added_feature_) {
		double reward_value, weight, total_reward = 0;
		for (int i = 0; i < features_.size(); i++) {
			features_[i]->computeReward(reward_value, terrain_info);
			features_[i]->getWeight(weight);
			total_reward += weight * reward_value;
		}

		Cell cell;
		getCell(cell, total_reward, terrain_info);
		addCellToRewardMap(cell);
	}
	else {
		printf(YELLOW "Could not compute the reward of the features because it is necessary to add at least one\n" COLOR_RESET);
	}
}


} //@namespace environment
} //@namespace dwl
