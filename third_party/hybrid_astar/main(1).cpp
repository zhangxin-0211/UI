#include <iostream>
#include <vector>
#include <cmath>
#include <map>
#include <string>
#include <algorithm>
#include <memory>
#include <QDebug>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===================== WGS84 常量 =====================
constexpr double WGS84_A = 6378137.0;
constexpr double WGS84_F = 1.0 / 298.257223563;
constexpr double WGS84_E2 = 2 * WGS84_F - WGS84_F * WGS84_F;


struct GridNode
{
    int ix, iy;
    // 增加无参默认构造
    GridNode() : ix(0), iy(0) {}
    GridNode(int x, int y) : ix(x), iy(y) {}
    bool operator<(const GridNode& other) const
    {
        if (ix != other.ix) return ix < other.ix;
        return iy < other.iy;
    }
};
// 经纬度结构（WGS84，单位：度）
struct GeoPoint
{
    double lon; // 经度 deg
    double lat; // 纬度 deg
    GeoPoint() : lon(0), lat(0) {}
    GeoPoint(double lo, double la) : lon(lo), lat(la) {}
};

// 地理位姿：经纬度 + 航向角（正北=0，向东顺时针，弧度）
struct GeoPose
{
    double lon;
    double lat;
    double yaw; // rad，地理航向：北0，东+
    GeoPose() : lon(0), lat(0), yaw(0) {}
    GeoPose(double lo, double la, double th) : lon(lo), lat(la), yaw(th) {}
};

// 局部ENU平面坐标（单位 m，E东，N北）
struct EnuPoint
{
    double e;
    double n;
    EnuPoint() : e(0), n(0) {}
    EnuPoint(double e_, double n_) : e(e_), n(n_) {}
};

struct EnuPose
{
    double e;
    double n;
    double yaw; // 局部平面航向，和地理航向一致(rad)
    EnuPose() : e(0), n(0), yaw(0) {}
    EnuPose(double e_, double n_, double th) : e(e_), n(n_), yaw(th) {}
};

// ===================== WGS84 <-> ENU 转换工具函数 =====================
// deg -> rad
inline double deg2rad(double deg) { return deg * M_PI / 180.0; }
inline double rad2deg(double rad) { return rad * 180.0 / M_PI; }

// 计算卯酉圈半径
double calcN(double lat_rad)
{
    double sinlat = sin(lat_rad);
    return WGS84_A / sqrt(1.0 - WGS84_E2 * sinlat * sinlat);
}
// 计算子午圈半径
double calcM(double lat_rad)
{
    double sinlat = sin(lat_rad);
    double tmp = 1.0 - WGS84_E2 * sinlat * sinlat;
    return WGS84_A * (1 - WGS84_E2) / pow(tmp, 1.5);
}

/**
 * @brief WGS84 经纬度转局部ENU平面，origin为局部原点
 */
EnuPoint geo2enu(const GeoPoint& origin, const GeoPoint& pt)
{
    double lat0 = deg2rad(origin.lat);
    double lon0 = deg2rad(origin.lon);
    double lat  = deg2rad(pt.lat);
    double lon  = deg2rad(pt.lon);

    double dlat = lat - lat0;
    double dlon = lon - lon0;

    double N0 = calcN(lat0);
    double M0 = calcM(lat0);

    double e = N0 * cos(lat0) * dlon;
    double n = M0 * dlat;
    return EnuPoint(e, n);
}

/**
 * @brief ENU局部平面转回WGS84经纬度
 */
GeoPoint enu2geo(const GeoPoint& origin, const EnuPoint& enu)
{
    double lat0 = deg2rad(origin.lat);
    double lon0 = deg2rad(origin.lon);
    double N0 = calcN(lat0);
    double M0 = calcM(lat0);

    double dlat = enu.n / M0;
    double dlon = enu.e / (N0 * cos(lat0));

    double lat = lat0 + dlat;
    double lon = lon0 + dlon;
    return GeoPoint(rad2deg(lon), rad2deg(lat));
}

// ===================== 下面全部是平面规划结构（单位米，逻辑不变） =====================
struct Primitive
{
    double R;
    double length;
    double cost_pen;
    Primitive(double r, double l, double c) : R(r), length(l), cost_pen(c) {}
};

struct Node
{
    EnuPose pose;
    double g;
    double h;
    double f;
    Node* parent;
    Node() : g(0), h(0), f(0), parent(nullptr) {}
    Node(EnuPose p) : pose(p), g(0), h(0), f(0), parent(nullptr) {}
};



inline double wrapToPi(double angle)
{
    while (angle > M_PI)  angle -= 2.0 * M_PI;
    while (angle < -M_PI) angle += 2.0 * M_PI;
    return angle;
}

inline double wrapTo2Pi(double angle)
{
    double a = fmod(angle, 2.0 * M_PI);
    if (a < 0) a += 2.0 * M_PI;
    return a;
}

double pointSegDist(double px, double py, double x0, double y0, double x1, double y1)
{
    double vx = x1 - x0;
    double vy = y1 - y0;
    double wx = px - x0;
    double wy = py - y0;
    double c1 = vx * wx + vy * wy;
    if (c1 <= 1e-6) return hypot(px - x0, py - y0);
    double c2 = vx * vx + vy * vy;
    if (c2 <= c1) return hypot(px - x1, py - y1);
    double t = c1 / c2;
    double projX = x0 + t * vx;
    double projY = y0 + t * vy;
    return hypot(px - projX, py - projY);
}

std::vector<EnuPoint> astar2d(const EnuPoint& start_xy, const EnuPoint& goal_xy,
                              const std::vector<EnuPoint>& obs, double world_max, double grid_step, double obs_safe)
{
    qDebug()<<"start enu:"<<start_xy.e<<","<<start_xy.n;
    qDebug()<<"goal enu:"<<goal_xy.e<<","<<goal_xy.n;
    qDebug()<<"world_max:"<<world_max;

    auto idx = [&](double p) -> int { return static_cast<int>(round(p / grid_step)); };
    int maxI = idx(world_max);
    std::map<GridNode, double> g_map;
    std::map<GridNode, GridNode> parent_map;
    std::vector<GridNode> open_set;
    std::vector<std::vector<bool>> closed(maxI + 5, std::vector<bool>(maxI + 5, false));

    int sx = idx(start_xy.e) + 1;
    int sy = idx(start_xy.n) + 1;
    int gx = idx(goal_xy.e) + 1;
    int gy = idx(goal_xy.n) + 1;
    GridNode start_g(sx, sy);
    g_map[start_g] = 0.0;
    open_set.emplace_back(sx, sy);

    int dir8[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
    bool found = false;
    GridNode goal_g(gx, gy);

    while (!open_set.empty())
    {
        int min_id = 0;
        double min_f = 1e18;
        for (size_t k = 0; k < open_set.size(); k++)
        {
            auto& nd = open_set[k];
            double xw = (nd.ix - 1) * grid_step;
            double yw = (nd.iy - 1) * grid_step;
            double h = hypot(xw - goal_xy.e, yw - goal_xy.n);
            double f = g_map[nd] + h;
            if (f < min_f)
            {
                min_f = f;
                min_id = static_cast<int>(k);
            }
        }
        GridNode curr = open_set[min_id];
        open_set.erase(open_set.begin() + min_id);
        if (curr.ix == goal_g.ix && curr.iy == goal_g.iy)
        {
            goal_g = curr;
            found = true;
            break;
        }
        if (closed[curr.ix][curr.iy]) continue;
        closed[curr.ix][curr.iy] = true;

        for (int d = 0; d < 8; d++)
        {
            int nx = curr.ix + dir8[d][0];
            int ny = curr.iy + dir8[d][1];
            if (nx < 1 || ny < 1 || nx > maxI + 2 || ny > maxI + 2) continue;
            if (closed[nx][ny]) continue;
            double xw = (nx - 1) * grid_step;
            double yw = (ny - 1) * grid_step;
            bool collide = false;
            for (auto& o : obs)
            {
                if (hypot(xw - o.e, yw - o.n) < obs_safe)
                {
                    collide = true;
                    break;
                }
            }
            if (collide) continue;
            double cost = hypot(dir8[d][0], dir8[d][1]) * grid_step;
            GridNode next_g(nx, ny);
            double new_g = g_map[curr] + cost;
            if (!g_map.count(next_g) || new_g < g_map[next_g])
            {
                g_map[next_g] = new_g;
                parent_map[next_g] = curr;
                open_set.push_back(next_g);
            }
        }
    }
    std::vector<EnuPoint> path;
    if (!found) return path;
    GridNode p = goal_g;
    while (true)
    {
        double wx = (p.ix - 1) * grid_step;
        double wy = (p.iy - 1) * grid_step;
        path.emplace_back(wx, wy);
        if (!parent_map.count(p)) break;
        p = parent_map[p];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

bool inCorridor(double px, double py, const std::vector<EnuPoint>& ref, double halfW)
{
    for (size_t i = 0; i + 1 < ref.size(); i++)
    {
        double d = pointSegDist(px, py, ref[i].e, ref[i].n, ref[i+1].e, ref[i+1].n);
        if (d < halfW) return true;
    }
    return false;
}

EnuPose propagate(const EnuPose& curr, double R, double s_len)
{
    EnuPose res = curr;
    double x0 = curr.e;
    double y0 = curr.n;
    double th0 = curr.yaw;
    if (std::isinf(R))
    {
        res.e = x0 + s_len * sin(th0);
        res.n = y0 + s_len * cos(th0);
        res.yaw = th0;
    }
    else
    {
        double dth = s_len / R;
        res.e = x0 + R * (cos(th0) - cos(th0 + dth));
        res.n = y0 + R * (sin(th0 + dth) - sin(th0));
        res.yaw = wrapToPi(th0 + dth);
    }
    return res;
}

bool checkCollision(const EnuPose& from, const EnuPose& to, const std::vector<EnuPoint>& obs, double obs_safe, int sample_num = 10)
{
    for (int i = 0; i <= sample_num; i++)
    {
        double t = static_cast<double>(i) / sample_num;
        double ei = from.e + t * (to.e - from.e);
        double ni = from.n + t * (to.n - from.n);
        for (auto& o : obs)
        {
            if (hypot(ei - o.e, ni - o.n) < obs_safe)
            {
                return true;
            }
        }
    }
    return false;
}

double dubinsHeuristic(double e, double n, double th, double ge, double gn, double* gth, double Rmin)
{
    double d_euc = hypot(ge - e, gn - n);
    if (gth == nullptr) return d_euc;
    double d_ang = fabs(wrapToPi(*gth - th)) * Rmin;
    return std::max(d_euc, d_ang);
}

std::string makeKey(double e, double n, double th, double grid_xy, int n_theta)
{
    double ek = round(e / grid_xy) * grid_xy;
    double nk = round(n / grid_xy) * grid_xy;
    int tidx = static_cast<int>(round(wrapTo2Pi(th) / (2.0 * M_PI) * n_theta)) % n_theta;
    char buf[128];
    sprintf(buf, "%.0f_%.0f_%d", ek, nk, tidx);
    return std::string(buf);
}

std::vector<EnuPose> hybridAstarLocal(const EnuPose& start_pose,
                                      const EnuPoint& goal_xy,
                                      double* goal_yaw,
                                      const std::vector<EnuPoint>& obs,
                                      const std::vector<EnuPoint>& ref_path,
                                      double corridor_half,
                                      double Rmin,
                                      const std::vector<Primitive>& prims,
                                      int n_theta,
                                      double grid_xy,
                                      int max_iter,
                                      double obs_safe)
{
    std::vector<Node*> open_list;
    std::map<std::string, bool> closed_map;
    Node* start_node = new Node(start_pose);
    start_node->g = 0.0;
    start_node->h = dubinsHeuristic(start_pose.e, start_pose.n, start_pose.yaw, goal_xy.e, goal_xy.n, goal_yaw, Rmin);
    start_node->f = start_node->g + start_node->h;
    open_list.push_back(start_node);

    const double xy_tol = 12.0;
    const double th_tol = 0.25;
    int iter_cnt = 0;
    std::vector<EnuPose> result;

    while (!open_list.empty() && iter_cnt < max_iter)
    {
        iter_cnt++;
        int min_idx = 0;
        double min_f = 1e18;
        for (size_t k = 0; k < open_list.size(); k++)
        {
            if (open_list[k]->f < min_f)
            {
                min_f = open_list[k]->f;
                min_idx = static_cast<int>(k);
            }
        }
        Node* curr = open_list[min_idx];
        open_list.erase(open_list.begin() + min_idx);

        double d_goal = hypot(curr->pose.e - goal_xy.e, curr->pose.n - goal_xy.n);
        bool angle_ok = true;
        if (goal_yaw != nullptr)
        {
            angle_ok = fabs(wrapToPi(curr->pose.yaw - *goal_yaw)) < th_tol;
        }
        if (d_goal < xy_tol && angle_ok)
        {
            Node* p = curr;
            while (p != nullptr)
            {
                result.insert(result.begin(), p->pose);
                p = p->parent;
            }
            for (auto nd : open_list) delete nd;
            return result;
        }
        std::string key = makeKey(curr->pose.e, curr->pose.n, curr->pose.yaw, grid_xy, n_theta);
        if (closed_map.count(key)) continue;
        closed_map[key] = true;

        for (auto& prim : prims)
        {
            EnuPose next_pose = propagate(curr->pose, prim.R, prim.length);
            if (!inCorridor(next_pose.e, next_pose.n, ref_path, corridor_half)) continue;
            if (checkCollision(curr->pose, next_pose, obs, obs_safe)) continue;

            Node* next_node = new Node(next_pose);
            next_node->g = curr->g + prim.length + prim.cost_pen;
            next_node->h = dubinsHeuristic(next_pose.e, next_pose.n, next_pose.yaw, goal_xy.e, goal_xy.n, goal_yaw, Rmin);
            next_node->f = next_node->g + next_node->h;
            next_node->parent = curr;
            open_list.push_back(next_node);
        }
    }
    for (auto nd : open_list) delete nd;
    return {};
}

// ===================== 对外顶层接口：全部经纬度 =====================
/**
 * @brief Hybrid?A* 地理路径规划（WGS84经纬度）
 * @param start 起点：lon(°),lat(°),yaw(rad) 正北=0向东增加
 * @param goal 终点：lon(°),lat(°),yaw(rad)
 * @param obstacles 障碍物WGS84经纬度点，安全膨胀半径固定5米
 * @return 整条地理轨迹（经纬度+航向）
 */
std::vector<GeoPose> planGeoHybridAstar(const GeoPose& start, const GeoPose& goal, const std::vector<GeoPoint>& obstacles)
{
    // 局部ENU原点 = 起点位置
    GeoPoint origin(start.lon, start.lat);

    // 1. Geo -> ENU平面
    EnuPoint start_enu_pt = geo2enu(origin, GeoPoint(start.lon, start.lat));
    EnuPose start_enu(start_enu_pt.e, start_enu_pt.n, start.yaw);

    EnuPoint goal_enu_pt = geo2enu(origin, GeoPoint(goal.lon, goal.lat));
    EnuPose goal_enu(goal_enu_pt.e, goal_enu_pt.n, goal.yaw);

    std::vector<EnuPoint> obs_enu;
    for (auto& g : obstacles)
    {
        obs_enu.push_back(geo2enu(origin, g));
    }

    // 固定规划参数（米制约束不变）
    const double Rmin = 5.0;
    const double L_straight = 12.0;
    const double L_arc_s = 6.0;
    const double L_arc_l = 9.0;
    const double cost_s = 2.0;
    const double cost_l = 5.0;
    const int n_theta = 32;
    const double grid_xy = 10.0;
    const double window_len = 200.0;
    const double corridor0 = 70.0;
    const double obs_safe = 3.0; // 障碍物安全距离5米
    const int max_iter_local = 20000;
    const double world_max = 5000.0;
    const double coarse_grid = 10.0;

    std::vector<Primitive> primitives;
    primitives.emplace_back(INFINITY, L_straight, 0.0);
    primitives.emplace_back(Rmin, L_arc_s, cost_s);
    primitives.emplace_back(-Rmin, L_arc_s, cost_s);
    primitives.emplace_back(Rmin, L_arc_l, cost_l);
    primitives.emplace_back(-Rmin, L_arc_l, cost_l);

    std::vector<EnuPoint> coarse_ref = astar2d(start_enu_pt, goal_enu_pt, obs_enu, world_max, coarse_grid, obs_safe);
    if (coarse_ref.empty())
    {
        std::cerr << "Coarse geo path not found!" << std::endl;
        return {};
    }

    std::vector<EnuPose> full_enu_traj;
    EnuPose cur_pose = start_enu;
    size_t seg_ptr = 0;
    size_t n_ref = coarse_ref.size();

    while (true)
    {
        double dist_acc = 0.0;
        size_t win_end = seg_ptr;
        for (size_t k = seg_ptr; k + 1 < n_ref; k++)
        {
            dist_acc += hypot(coarse_ref[k+1].e - coarse_ref[k].e, coarse_ref[k+1].n - coarse_ref[k].n);
            win_end = k + 1;
            if (dist_acc > window_len) break;
        }
        std::vector<EnuPoint> local_ref(coarse_ref.begin() + seg_ptr, coarse_ref.begin() + win_end + 1);
        EnuPoint seg_goal_xy = coarse_ref[win_end];

        std::vector<EnuPose> local_traj = hybridAstarLocal(cur_pose, seg_goal_xy, nullptr, obs_enu, local_ref, corridor0,
                                                           Rmin, primitives, n_theta, grid_xy, max_iter_local, obs_safe);
        if (local_traj.empty())
        {
            std::cerr << "Local segment fail, widen corridor retry\n";
            local_traj = hybridAstarLocal(cur_pose, seg_goal_xy, nullptr, obs_enu, local_ref, corridor0 + 40,
                                          Rmin, primitives, n_theta, grid_xy, max_iter_local, obs_safe);
            if (local_traj.empty())
            {
                std::cerr << "Segment planning failed\n";
                break;
            }
        }
        if (!full_enu_traj.empty() && !local_traj.empty()) local_traj.erase(local_traj.begin());
        full_enu_traj.insert(full_enu_traj.end(), local_traj.begin(), local_traj.end());
        cur_pose = full_enu_traj.back();
        seg_ptr = win_end;

        if (hypot(cur_pose.e - goal_enu.e, cur_pose.n - goal_enu.n) < 30.0)
        {
            double gyaw = goal.yaw;
            std::vector<EnuPose> final_seg = hybridAstarLocal(cur_pose, goal_enu_pt, &gyaw, obs_enu,
                                                              std::vector<EnuPoint>(coarse_ref.begin() + seg_ptr, coarse_ref.end()),
                                                              corridor0, Rmin, primitives, n_theta, grid_xy, max_iter_local, obs_safe);
            if (!final_seg.empty())
            {
                final_seg.erase(final_seg.begin());
                full_enu_traj.insert(full_enu_traj.end(), final_seg.begin(), final_seg.end());
            }
            break;
        }
    }

    // ENU轨迹转回WGS84经纬度输出
    std::vector<GeoPose> geo_path;
    for (auto& enu_p : full_enu_traj)
    {
        GeoPoint gp = enu2geo(origin, EnuPoint(enu_p.e, enu_p.n));
        geo_path.emplace_back(gp.lon, gp.lat, enu_p.yaw);
    }
    return geo_path;
}

// Demo测试
int main()
{
    // 示例：起点终点障碍物全部使用经纬度（°）
    GeoPose start(113.94, 22.54, 0.0);
    GeoPose goal(113.96, 22.56, M_PI/3.0);
    std::vector<GeoPoint> obs = {
        {113.945, 22.545},
        {113.948, 22.548},
        {113.952, 22.552}
    };
    std::vector<GeoPose> geo_trajectory = planGeoHybridAstar(start, goal, obs);
    std::cout << "Total geo trajectory points: " << geo_trajectory.size() << "\n";
    for (auto& p : geo_trajectory)
    {
        printf("lon:%.6f lat:%.6f yaw:%.3f rad\n", p.lon, p.lat, p.yaw);
    }
    return 0;
}
