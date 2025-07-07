#define NOMINMAX // min, maxを使う時にWindowsの定義を無効化する

#include <Novice.h>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <map>

// SRPG用--------------------------
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <tuple>
#include <limits>

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
//---------------------------------

struct Vector2 {
	float x;
	float y;
};

struct Vector3 {
	float x;
	float y;
	float z;
};

struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
};

struct Matrix4x4 {
	float m[4][4];
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

// 単位行列の作成
Matrix4x4 MakeIdentity4x4();

// 4x4行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float angle);
// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float angle);
// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float angle);

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

// 逆行列
Matrix4x4 Inverse(const Matrix4x4& m);


///----------------------------------------------------------------------------
/// TR1_LLM_SRPG用の設定
///----------------------------------------------------------------------------

std::deque<std::string> combat_log;
constexpr size_t MAX_LOG_SIZE = 10;

// ------------------------
// マップとユニット情報
// ------------------------
constexpr int TILE_SIZE = 32; // タイルのサイズ
constexpr int MAP_SIZE = 16;  // マップのサイズ(16x16)

// タイルの種類
enum TileType {
	PLAIN = 0,  // 平地
	FOREST = 1  // 森
};

// ターンのフェーズ
enum Phase {
	PlayerTurn, // プレイヤーターン
	EnemyTurn   // エネミーターン
};
Phase current_phase = PlayerTurn; // 現在のフェーズ(開始時はプレイヤーターン)

// ユニットの武器タイプ
enum class WeaponType {
	Sword,      // 剣(近接)
	Bow         // 弓(遠距離)
};

// マップの定義
int map[MAP_SIZE][MAP_SIZE] = {
//   0 1 2 3 4 5 6 7 8 9.0.1.2.3.4.5
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//0
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//1
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//2
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//3
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//4
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//5
	{0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0},//6
	{0,0,0,0,0,1,1,0,1,0,1,0,0,0,0,0},//7
	{0,0,0,0,0,1,0,1,0,1,1,0,0,0,0,0},//8
	{0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0},//9
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//10
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//11
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//12
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//13
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//14
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//15
};

// ユニット情報
struct Unit {
	std::string name;          // ユニット名
	int x, y;                  // 位置
	bool is_enemy;             // 敵かどうか
	int hp;                    // hp
	int move = 3;              // 移動力
	bool has_moved = false;    // 移動済みかどうか
	bool has_attacked = false; // 攻撃済みかどうか
	int atk = 5;               // 攻撃力
	int def = 2;               // 防御力

	WeaponType weapon = WeaponType::Sword; // 武器タイプ

	// 最小攻撃範囲
	int min_range() const {
		return weapon == WeaponType::Sword ? 1 : 2;
	}
	// 最大攻撃範囲
	int max_range() const {
		return weapon == WeaponType::Sword ? 1 : 2;
	}
};

// ユニットの初期化
std::vector<Unit> units = {
	{"ally1", 9, 12, false, 20, 3, false, false, 7, 3, WeaponType::Sword},  // 味方ユニット
	{"ally2", 6, 12, false, 15, 2, false, false, 5, 2, WeaponType::Bow},
	{"enemy1", 6, 3, true, 20, 3, false, false, 7, 3, WeaponType::Sword},  // 敵ユニット
	{"enemy2", 9, 3, true, 15, 2, false, false, 5, 2, WeaponType::Bow}
};

int selected_unit_index = -1;  // 選択中のユニットインデックス
std::set<std::pair<int, int>> current_move_range; // 現在の移動可能範囲(ユニットの移動力に基づく)
std::set<std::pair<int, int>> current_attack_range; // 現在の攻撃可能範囲(ユニットの攻撃範囲に基づく)

// ------------------------
// ImGui関数
// ------------------------

// マップのサイズとタイルのサイズを基に、ウィンドウのクライアント領域を設定
bool is_within_bounds(int x, int y) {
	return x >= 0 && y >= 0 && x < MAP_SIZE && y < MAP_SIZE;
}

// タイルが進行可能かどうかを判定する関数
bool is_tile_passable(int x, int y) {
	return map[y][x] != FOREST; // 森を通れない
}

// ユニットがその位置にいるかどうかを判定する関数
bool is_occupied(int x, int y) {
	for (const auto& u : units) {
		if (u.hp > 0 && u.x == x && u.y == y) return true;
	}
	return false;
}

// ユニットの移動範囲を計算する関数
std::set<std::pair<int, int>> get_move_range(const Unit& unit) {
	std::set<std::pair<int, int>> result;
	std::queue<std::tuple<int, int, int>> q;
	q.push({ unit.x, unit.y, 0 });

	while (!q.empty()) {
		auto [x, y, d] = q.front(); q.pop();
		if (d > unit.move) continue;
		if (!is_within_bounds(x, y) || !is_tile_passable(x, y)) continue;
		if (result.count({ x, y })) continue;
		result.insert({ x, y });

		q.push({ x + 1, y, d + 1 });
		q.push({ x - 1, y, d + 1 });
		q.push({ x, y + 1, d + 1 });
		q.push({ x, y - 1, d + 1 });
	}
	return result;
}

// ユニットの攻撃範囲を計算する関数
std::set<std::pair<int, int>> get_attack_range(const Unit& unit) {
	std::set<std::pair<int, int>> result;
	for (int dx = -unit.max_range(); dx <= unit.max_range(); ++dx) {
		for (int dy = -unit.max_range(); dy <= unit.max_range(); ++dy) {
			int dist = abs(dx) + abs(dy);
			if (dist >= unit.min_range() && dist <= unit.max_range()) {
				int tx = unit.x + dx;
				int ty = unit.y + dy;
				if (is_within_bounds(tx, ty)) {
					result.insert({ tx, ty });
				}
			}
		}
	}
	return result;
}

void log(const std::string& msg) {
	combat_log.push_front(msg);
	if (combat_log.size() > MAX_LOG_SIZE) combat_log.pop_back();
}

// ユニットを攻撃する関数
void attack(Unit& attacker, Unit& target) {
	int damage = std::max(0, attacker.atk - target.def);
	target.hp -= damage;
	log(attacker.name + " Attack! " + target.name + " Deals " + std::to_string(damage) + " Damage ");

	if (target.hp <= 0) {
		log(target.name + " Is Defeted ");
		return;
	}

	// 反撃処理
	if (target.hp > 0) {
		auto counter_range = get_attack_range(target);
		if (counter_range.count({ attacker.x, attacker.y })) {
			int counter = std::max(0, target.atk - attacker.def);
			attacker.hp -= counter;
			log(target.name + " Counter! " + attacker.name + " Deals " + std::to_string(counter) + " Damage!! ");
			if (attacker.hp <= 0) log(attacker.name + " Is Defeted ");
		}
	}
}

// プレイヤーターンを終了する関数
void end_player_turn() {
	for (auto& u : units) {
		if (!u.is_enemy) {
			u.has_moved = false;
			u.has_attacked = false;
		}
	}
	current_phase = EnemyTurn;
}

// エネミーターンのロジック
void enemy_turn_logic() {
	for (auto& enemy : units) {
		if (!enemy.is_enemy || enemy.hp <= 0) continue;

		// 最も近いプレイヤーユニットを探す
		int closest_dist = std::numeric_limits<int>::max();
		Unit* target_unit = nullptr;

		for (auto& ally : units) {
			if (ally.is_enemy || ally.hp <= 0) continue;
			int dist = std::abs(enemy.x - ally.x) + std::abs(enemy.y - ally.y);
			if (dist < closest_dist) {
				closest_dist = dist;
				target_unit = &ally;
			}
		}

		if (target_unit) {
			// 射程内なら攻撃
			int dx = std::abs(enemy.x - target_unit->x);
			int dy = std::abs(enemy.y - target_unit->y);
			int dist = dx + dy;
			int min_r = enemy.min_range();
			int max_r = enemy.max_range();
			if (dist >= min_r && dist <= max_r) {
				attack(enemy, *target_unit);
				continue;
			}


			// 移動可能なマスを全て洗い出す
			std::set<std::pair<int, int>> possible_moves = get_move_range(enemy);

			int best_move_x = enemy.x;
			int best_move_y = enemy.y;
			Unit* best_attack_target = nullptr; // 移動後に攻撃するターゲット
			int max_potential_damage = -1; // 移動後に与えられる最大ダメージ

			// 移動先の候補地を評価
			for (const auto& move_pos : possible_moves) {
				// 移動後の位置から攻撃できる敵を探す
				// マンハッタン距離で判定

				for (auto& ally : units) {
					if (ally.is_enemy || ally.hp <= 0) continue;

					int new_dx = std::abs(move_pos.first - ally.x);
					int new_dy = std::abs(move_pos.second - ally.y);
					int new_dist = new_dx + new_dy;

					if (new_dist >= min_r && new_dist <= max_r) {
						// 攻撃可能なターゲットが見つかった場合
						int current_potential_damage = std::max(0, enemy.atk - ally.def);
						if (current_potential_damage > max_potential_damage) {
							max_potential_damage = current_potential_damage;
							best_move_x = move_pos.first;
							best_move_y = move_pos.second;
							best_attack_target = &ally; // このターゲットを攻撃する
						}
					}
				}
			}

			// 最適な移動と攻撃を実行
			if (best_attack_target && max_potential_damage > 0) { // 攻撃可能なユニットが見つかった場合
				enemy.x = best_move_x;
				enemy.y = best_move_y;
				attack(enemy, *best_attack_target);
			} else {
				// 攻撃可能な場所が見つからなかった場合、ターゲットに近づく
				// ターゲットまでの距離が最も短くなる1マス移動先を探す
				int current_dist_to_target = std::abs(enemy.x - target_unit->x) + std::abs(enemy.y - target_unit->y);
				int best_approach_x = enemy.x;
				int best_approach_y = enemy.y;

				const int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

				for (auto& dir : dirs) {
					int nx = enemy.x + dir[0];
					int ny = enemy.y + dir[1];
					if (!is_within_bounds(nx, ny)) continue;
					// 敵ユニットの移動では、他のユニットが占拠していても通過できる（隣接マスへの移動のみ）
					// is_occupiedチェックは不要かもしれないが、ここでは残す
					if (!is_tile_passable(nx, ny) || is_occupied(nx, ny)) continue;

					int new_dist_to_target = std::abs(nx - target_unit->x) + std::abs(ny - target_unit->y);
					if (new_dist_to_target < current_dist_to_target) {
						current_dist_to_target = new_dist_to_target;
						best_approach_x = nx;
						best_approach_y = ny;
					}
				}
				enemy.x = best_approach_x;
				enemy.y = best_approach_y;
			}
		}
	}
	for (auto& u : units) u.has_moved = u.has_attacked = false;
	current_phase = PlayerTurn;
}

// マップとユニットを描画する関数
void RenderMapWithUnits() {
	ImGui::Begin("Tactics Map");

	ImDrawList* draw_list = ImGui::GetWindowDrawList(); // 描画リストを取得
	ImVec2 origin = ImGui::GetCursorScreenPos();        // カーソルの位置を取得

	// マップの描画
	for (int y = 0; y < MAP_SIZE; ++y) {
		for (int x = 0; x < MAP_SIZE; ++x) {
			ImU32 color = (map[y][x] == 0) ? IM_COL32(200, 200, 200, 255) : IM_COL32(100, 200, 100, 255); // 平地と森の色
			// マスの移動可能範囲
			if (current_move_range.count({ x, y })) color = IM_COL32(100, 100, 255, 180);
			// マスの攻撃可能範囲
			if (current_attack_range.count({ x, y })) color = IM_COL32(255, 100, 100, 180);
			ImVec2 tl = { origin.x + x * TILE_SIZE, origin.y + y * TILE_SIZE };
			ImVec2 br = { tl.x + TILE_SIZE, tl.y + TILE_SIZE };
			draw_list->AddRectFilled(tl, br, color);
			draw_list->AddRect(tl, br, IM_COL32(0, 0, 0, 255));
		}
	}

	// ユニットの描画
	for (size_t i = 0; i < units.size(); ++i) {
		const auto& u = units[i];
		if (u.hp <= 0) continue; // HPが0のユニットは描画しない
		ImVec2 tl = { origin.x + u.x * TILE_SIZE, origin.y + u.y * TILE_SIZE };
		ImVec2 br = { tl.x + TILE_SIZE, tl.y + TILE_SIZE };
		ImU32 color = u.is_enemy ? IM_COL32(255, 50, 50, 255) : IM_COL32(50, 50, 255, 255);
		draw_list->AddRectFilled(tl, br, color);
		if ((int)i == selected_unit_index) draw_list->AddRect(tl, br, IM_COL32(255, 255, 0, 255), 0.0f, 0, 3.0f);
	}

	// マスクリック処理
	ImVec2 mouse = ImGui::GetMousePos();
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
		int mx = (int)((mouse.x - origin.x) / TILE_SIZE);
		int my = (int)((mouse.y - origin.y) / TILE_SIZE);

		// 既にユニットが選択されていて、移動範囲内なら移動処理
		if (selected_unit_index >= 0 && selected_unit_index < (int)units.size()) {
			auto& u = units[selected_unit_index];
			if (u.hp > 0) {
				if (!u.has_moved && current_move_range.count({ mx, my }) && !is_occupied(mx, my)) {
					u.x = mx;
					u.y = my;
					u.has_moved = true;
					current_move_range.clear();
					current_attack_range = get_attack_range(u);
				} else if (!u.has_attacked && current_attack_range.count({ mx, my })) {
					// 攻撃処理
					for (auto& target : units) {
						if (target.is_enemy && target.hp > 0 && target.x == mx && target.y == my) {
							attack(u, target);
							u.has_attacked = true;
							break;
						}
					}
				}
			}
		}

		// ユニット選択
		for (size_t i = 0; i < units.size(); ++i) {
			if (units[i].x == mx && units[i].y == my && !units[i].is_enemy && units[i].hp > 0) {
				selected_unit_index = (int)i;
				current_move_range = get_move_range(units[i]);
			}
		}
	}

	ImGui::Dummy(ImVec2(MAP_SIZE * TILE_SIZE, MAP_SIZE * TILE_SIZE));
	ImGui::End();
}

// ユニットパネルを描画する関数
void RenderUnitPanel() {
	ImGui::Begin("Unit Info");
	if (selected_unit_index >= 0 && selected_unit_index < (int)units.size()) {
		auto& u = units[selected_unit_index];
		if (u.hp > 0) {
			ImGui::Text("%s", u.name.c_str());
			ImGui::Text("Position: (%d, %d)", u.x, u.y);
			ImGui::Text("HP: %d", u.hp);
			ImGui::Text("ATK: %d / DEF: %d", u.atk, u.def);
			ImGui::Text("Moved: %s", u.has_moved ? "Yes" : "No");
			ImGui::Text("Attacked: %s", u.has_attacked ? "Yes" : "No");
		} else {
			ImGui::Text("[Unit Is Defeted] %s", u.name.c_str());
		}
	} else {
		ImGui::Text("Please Select");
	}
	if (current_phase == PlayerTurn && ImGui::Button("Turn End")) {
		end_player_turn();
	}
	ImGui::End();
}

// 戦闘ログを描画する関数
void RenderCombatLog() {
	ImGui::Begin("CombatLog");
	for (const auto& entry : combat_log) {
		ImGui::TextWrapped("%s", entry.c_str());
	}
	ImGui::End();
}

// UIを描画する関数
void RenderUI() {
	if (current_phase == EnemyTurn) enemy_turn_logic();
	RenderMapWithUnits();
	RenderUnitPanel();
	RenderCombatLog();
}

///----------------------------------------------------------------------------

const char kWindowTitle[] = "SRPG_With_LLM";

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// ライブラリの初期化
	Novice::Initialize(kWindowTitle, 1280, 720);

	// キー入力結果を受け取る箱
	char keys[256] = {0};
	char preKeys[256] = {0};

	// ウィンドウの×ボタンが押されるまでループ
	while (Novice::ProcessMessage() == 0) {
		// フレームの開始
		Novice::BeginFrame();

		// キー入力を受け取る
		memcpy(preKeys, keys, 256);
		Novice::GetHitKeyStateAll(keys);

		///
		/// ↓更新処理ここから
		///

		///----------------------------------------------------------------------------
			/// TR1_LLM_SRPG用の設定
			///----------------------------------------------------------------------------

		RenderUI();

		///
		/// ↑更新処理ここまで
		///

		///
		/// ↓描画処理ここから
		///


		///
		/// ↑描画処理ここまで
		///

		// フレームの終了
		Novice::EndFrame();

		// ESCキーが押されたらループを抜ける
		if (preKeys[DIK_ESCAPE] == 0 && keys[DIK_ESCAPE] != 0) {
			break;
		}
	}

	// ライブラリの終了
	Novice::Finalize();
	return 0;
}

// 単位行列の作成
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (i == j) {
				result.m[i][j] = 1.0f;
			} else {
				result.m[i][j] = 0.0f;
			}
		}
	}
	return result;
}

// 4x4行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = 0;
			for (int k = 0; k < 4; k++) {
				result.m[i][j] += m1.m[i][k] * m2.m[k][j];
			}
		}
	}
	return result;
}

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float angle) {
	Matrix4x4 result = {};
	result.m[0][0] = 1.0f;
	result.m[3][3] = 1.0f;
	result.m[1][1] = std::cos(angle);
	result.m[1][2] = std::sin(angle);
	result.m[2][1] = -std::sin(angle);
	result.m[2][2] = std::cos(angle);
	return result;
}
// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float angle) {
	Matrix4x4 result = {};
	result.m[1][1] = 1.0f;
	result.m[3][3] = 1.0f;
	result.m[0][0] = std::cos(angle);
	result.m[0][2] = -std::sin(angle);
	result.m[2][0] = std::sin(angle);
	result.m[2][2] = std::cos(angle);
	return result;
}
// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float angle) {
	Matrix4x4 result = {};
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;
	result.m[0][0] = std::cos(angle);
	result.m[0][1] = std::sin(angle);
	result.m[1][0] = -std::sin(angle);
	result.m[1][1] = std::cos(angle);
	return result;
}

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 result = {};
	// X,Y,Z軸の回転をまとめる
	Matrix4x4 rotateXYZ =
		Multiply(MakeRotateXMatrix(rotate.x), Multiply(MakeRotateYMatrix(rotate.y), MakeRotateZMatrix(rotate.z)));

	result.m[0][0] = scale.x * rotateXYZ.m[0][0];
	result.m[0][1] = scale.x * rotateXYZ.m[0][1];
	result.m[0][2] = scale.x * rotateXYZ.m[0][2];
	result.m[1][0] = scale.y * rotateXYZ.m[1][0];
	result.m[1][1] = scale.y * rotateXYZ.m[1][1];
	result.m[1][2] = scale.y * rotateXYZ.m[1][2];
	result.m[2][0] = scale.z * rotateXYZ.m[2][0];
	result.m[2][1] = scale.z * rotateXYZ.m[2][1];
	result.m[2][2] = scale.z * rotateXYZ.m[2][2];
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	result.m[3][3] = 1.0f;

	return result;
}

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	Matrix4x4 result = {};
	result.m[0][0] = 1.0f / (aspectRatio * std::tan(fovY / 2.0f));
	result.m[1][1] = 1.0f / std::tan(fovY / 2.0f);
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = -(farClip * nearClip) / (farClip - nearClip);
	return result;
}

// 逆行列
Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 result;
	float determinant = 0;
	// 行列式を計算
	determinant =
		m.m[0][0] * m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[0][0] * m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[0][0] * m.m[1][3] * m.m[2][1] * m.m[3][2]
		- m.m[0][0] * m.m[1][3] * m.m[2][2] * m.m[3][1] - m.m[0][0] * m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[0][0] * m.m[1][1] * m.m[2][3] * m.m[3][2]
		- m.m[0][1] * m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[1][0] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[1][0] * m.m[2][1] * m.m[3][2]
		+ m.m[0][3] * m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[1][0] * m.m[2][3] * m.m[3][2]
		+ m.m[0][1] * m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[2][0] * m.m[3][2]
		- m.m[0][3] * m.m[1][2] * m.m[2][0] * m.m[3][1] - m.m[0][2] * m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] * m.m[3][2]
		- m.m[0][1] * m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[0][2] * m.m[1][3] * m.m[2][1] * m.m[3][0] - m.m[0][3] * m.m[1][1] * m.m[2][2] * m.m[3][0]
		+ m.m[0][3] * m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[0][2] * m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[0][1] * m.m[1][3] * m.m[2][2] * m.m[3][0];

	// 逆行列を計算
	result.m[0][0] = (m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[1][3] * m.m[2][1] * m.m[3][2]
		- m.m[1][3] * m.m[2][2] * m.m[3][1] - m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[1][1] * m.m[2][3] * m.m[3][2]) / determinant;
	result.m[0][1] = (-m.m[0][1] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2]
		+ m.m[0][3] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[2][3] * m.m[3][2]) / determinant;
	result.m[0][2] = (m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[3][2]
		- m.m[0][3] * m.m[1][2] * m.m[3][1] - m.m[0][2] * m.m[1][1] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[3][2]) / determinant;
	result.m[0][3] = (-m.m[0][1] * m.m[1][2] * m.m[2][3] - m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2]
		+ m.m[0][3] * m.m[1][2] * m.m[2][1] + m.m[0][2] * m.m[1][1] * m.m[2][3] + m.m[0][1] * m.m[1][3] * m.m[2][2]) / determinant;

	result.m[1][0] = (-m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[1][3] * m.m[2][0] * m.m[3][2]
		+ m.m[1][3] * m.m[2][2] * m.m[3][0] + m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[1][0] * m.m[2][3] * m.m[3][2]) / determinant;
	result.m[1][1] = (m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] + m.m[0][3] * m.m[2][0] * m.m[3][2]
		- m.m[0][3] * m.m[2][2] * m.m[3][0] - m.m[0][2] * m.m[2][0] * m.m[3][3] - m.m[0][0] * m.m[2][3] * m.m[3][2]) / determinant;
	result.m[1][2] = (-m.m[0][0] * m.m[1][2] * m.m[3][3] - m.m[0][2] * m.m[1][3] * m.m[3][0] - m.m[0][3] * m.m[1][0] * m.m[3][2]
		+ m.m[0][3] * m.m[1][2] * m.m[3][0] + m.m[0][2] * m.m[1][0] * m.m[3][3] + m.m[0][0] * m.m[1][3] * m.m[3][2]) / determinant;
	result.m[1][3] = (m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] + m.m[0][3] * m.m[1][0] * m.m[2][2]
		- m.m[0][3] * m.m[1][2] * m.m[2][0] - m.m[0][2] * m.m[1][0] * m.m[2][3] - m.m[0][0] * m.m[1][3] * m.m[2][2]) / determinant;

	result.m[2][0] = (m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[1][3] * m.m[2][0] * m.m[3][1]
		- m.m[1][3] * m.m[2][1] * m.m[3][0] - m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[1][0] * m.m[2][3] * m.m[3][1]) / determinant;
	result.m[2][1] = (-m.m[0][0] * m.m[2][1] * m.m[3][3] - m.m[0][1] * m.m[2][3] * m.m[3][0] - m.m[0][3] * m.m[2][0] * m.m[3][1]
		+ m.m[0][3] * m.m[2][1] * m.m[3][0] + m.m[0][1] * m.m[2][0] * m.m[3][3] + m.m[0][0] * m.m[2][3] * m.m[3][1]) / determinant;
	result.m[2][2] = (m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] + m.m[0][3] * m.m[1][0] * m.m[3][1]
		- m.m[0][3] * m.m[1][1] * m.m[3][0] - m.m[0][1] * m.m[1][0] * m.m[3][3] - m.m[0][0] * m.m[1][3] * m.m[3][1]) / determinant;
	result.m[2][3] = (-m.m[0][0] * m.m[1][1] * m.m[2][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] - m.m[0][3] * m.m[1][0] * m.m[2][1]
		+ m.m[0][3] * m.m[1][1] * m.m[2][0] + m.m[0][1] * m.m[1][0] * m.m[2][3] + m.m[0][0] * m.m[1][3] * m.m[2][1]) / determinant;

	result.m[3][0] = (-m.m[1][0] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1]
		+ m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[1][1] * m.m[2][0] * m.m[3][2] + m.m[1][0] * m.m[2][2] * m.m[3][1]) / determinant;
	result.m[3][1] = (m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] + m.m[0][2] * m.m[2][0] * m.m[3][1]
		- m.m[0][2] * m.m[2][1] * m.m[3][0] - m.m[0][1] * m.m[2][0] * m.m[3][2] - m.m[0][0] * m.m[2][2] * m.m[3][1]) / determinant;
	result.m[3][2] = (-m.m[0][0] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][2] * m.m[3][0] - m.m[0][2] * m.m[1][0] * m.m[3][1]
		+ m.m[0][2] * m.m[1][1] * m.m[3][0] + m.m[0][1] * m.m[1][0] * m.m[3][2] + m.m[0][0] * m.m[1][2] * m.m[3][1]) / determinant;
	result.m[3][3] = (m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] + m.m[0][2] * m.m[1][0] * m.m[2][1]
		- m.m[0][2] * m.m[1][1] * m.m[2][0] - m.m[0][1] * m.m[1][0] * m.m[2][2] - m.m[0][0] * m.m[1][2] * m.m[2][1]) / determinant;

	return result;
}