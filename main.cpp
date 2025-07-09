#define NOMINMAX // min, maxを使う時にWindowsの定義を無効化する
#define WIN32_LEAN_AND_MEAN // Windowsヘッダーを軽量化
#define _WINSOCKAPI_      // windows.h が winsock.h をインクルードするのを防ぐ

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
#include <ctime> // 時間取得のために追加

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

// LLM通信用に追加
#include "externals/nlohmann/json.hpp"
#ifdef _WIN32
#include <windows.h> // SetConsoleOutputCP, SetConsoleCtrlHandler のために必要
#endif
#include "externals/httplib.h"

//---------------------------------

// --- LLM関連の構造体と関数 (Ollamaプロジェクトからコピー) ---
// Ollama APIとのメッセージ形式
struct Message {
	std::string role;
	std::string content;
};

// Ollamaへのリクエストボディの構造
struct OllamaChatRequest {
	std::string model;
	std::vector<Message> messages;
	bool stream;
};

// Ollamaからの応答ボディの構造
struct OllamaChatResponse {
	std::string model;
	std::string created_at;
	Message message;
	bool done;
};

// nlohmann/json を使って構造体をJSONに変換・から変換するためのヘルパー
void to_json(nlohmann::json& j, const Message& m) {
	j = nlohmann::json{ {"role", m.role}, {"content", m.content} };
}
void from_json(const nlohmann::json& j, Message& m) {
	j.at("role").get_to(m.role);
	j.at("content").get_to(m.content);
}
void to_json(nlohmann::json& j, const OllamaChatRequest& req) {
	j = nlohmann::json{ {"model", req.model}, {"messages", req.messages}, {"stream", req.stream} };
}
void from_json(const nlohmann::json& j, OllamaChatResponse& res) {
	j.at("model").get_to(res.model);
	j.at("created_at").get_to(res.created_at);
	j.at("message").get_to(res.message);
	j.at("done").get_to(res.done);
}

// --- グローバル変数としてログファイルストリームとその他を宣言 ---
// これにより、シグナルハンドラ関数からアクセスできるようになります。
std::ofstream g_chatLogFile;
std::ofstream g_gameLogFile; // デバッグ/リプレイログを想定
std::vector<Message> g_conversationHistory; // 会話履歴もグローバルに
httplib::Client* g_ollamaClient = nullptr; // Ollamaクライアントへのポインタ

// --- フォルダ作成関数 (Ollamaプロジェクトからコピー) ---
bool createDirectory(const std::string& path) {
#ifdef _WIN32
	int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
	if (wlen == 0) return false;
	std::vector<wchar_t> wpath(wlen);
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

	if (!CreateDirectoryW(wpath.data(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		std::cerr << "エラー: フォルダ '" << path << "' を作成できませんでした。エラーコード: " << GetLastError() << std::endl;
		return false;
	}
	return true;
#else
	// POSIXシステム (Linux/macOS) の場合
	// #include <sys/stat.h> が必要
	// mkdir(path.c_str(), 0755); // 適切なパーミッションを設定
	// return true; // 簡略化
	// 環境によっては、より複雑なエラーチェックが必要です
	std::cerr << "警告: フォルダ作成はWindows専用の実装です。\n";
	return false;
#endif
}

// --- コンソール制御ハンドラ関数 (Ollamaプロジェクトからコピー) ---
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
	if (dwCtrlType == CTRL_C_EVENT ||
		dwCtrlType == CTRL_BREAK_EVENT ||
		dwCtrlType == CTRL_CLOSE_EVENT ||
		dwCtrlType == CTRL_LOGOFF_EVENT ||
		dwCtrlType == CTRL_SHUTDOWN_EVENT) {
		if (g_chatLogFile.is_open()) {
			g_chatLogFile << "\n--- 会話終了 (強制終了検出) ---\n\n";
			g_chatLogFile.flush();
			g_chatLogFile.close();
		}
		if (g_gameLogFile.is_open()) {
			g_gameLogFile << "\n--- ゲームログ終了 (強制終了検出) ---\n\n";
			g_gameLogFile.flush();
			g_gameLogFile.close();
		}
		ExitProcess(0);
	}
	return FALSE;
}

// 現在のタイムスタンプを取得するヘルパー関数
std::string getCurrentTimestamp() {
	time_t rawtime;
	struct tm timeinfo_struct;
	char buffer[80];
	time(&rawtime);
	if (localtime_s(&timeinfo_struct, &rawtime) == 0) {
		strftime(buffer, 80, "%Y%m%d_%H%M%S", &timeinfo_struct);
		return buffer;
	}
	return "unknown_time";
}

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
	//{"ally2", 6, 12, false, 15, 2, false, false, 5, 2, WeaponType::Bow},
	{"enemy1", 6, 3, true, 20, 3, false, false, 7, 3, WeaponType::Sword},  // 敵ユニット
	//{"enemy2", 9, 3, true, 15, 2, false, false, 5, 2, WeaponType::Bow}
};

int selected_unit_index = -1;  // 選択中のユニットインデックス
std::set<std::pair<int, int>> current_move_range; // 現在の移動可能範囲(ユニットの移動力に基づく)
std::set<std::pair<int, int>> current_attack_range; // 現在の攻撃可能範囲(ユニットの攻撃範囲に基づく)


// ------------------------
// ImGui関数
// ------------------------

void log(const std::string& msg) {
	combat_log.push_front(msg);
	if (combat_log.size() > MAX_LOG_SIZE) combat_log.pop_back();
}

// ------------------------
// SRPG関数
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

// ゲームの状態をAIプロンプトに変換する仮の関数
// 実際のゲームの状態に合わせて詳細に記述する必要がある
std::string getGameStateAsPrompt(const Unit& acting_unit) {
	std::string prompt = "Current game state:\n";
	prompt += "Map size: " + std::to_string(MAP_SIZE) + "x" + std::to_string(MAP_SIZE) + "\n";
	prompt += "Map layout (0=PLAIN, 1=FOREST):\n";
	for (int y = 0; y < MAP_SIZE; ++y) {
		for (int x = 0; x < MAP_SIZE; ++x) {
			prompt += std::to_string(map[y][x]) + " ";
		}
		prompt += "\n";
	}
	// マップ情報やユニット位置が提示される部分、またはアクションの例の前に挿入するのが良い
	prompt += "All coordinates for map positions (X, Y) are **0-indexed**, where (0,0) is the top-left corner of the map.\n";
	
	prompt += "Units:\n";
	for (const auto& u : units) {
		prompt += "- " + u.name + (u.is_enemy ? " (Enemy)" : " (Ally)") +
			" Pos: (" + std::to_string(u.x) + "," + std::to_string(u.y) + ")" +
			" HP: " + std::to_string(u.hp) +
			" ATK: " + std::to_string(u.atk) +
			" DEF: " + std::to_string(u.def) +
			" Moved: " + (u.has_moved ? "Yes" : "No") +
			" MoveRange: " + std::to_string(u.move) +
			" Attacked: " + (u.has_attacked ? "Yes" : "No") +
			" Weapon: " + (u.weapon == WeaponType::Sword ? "Sword" : "Bow") + 
			" AttackRange: " + (u.weapon == WeaponType::Sword ? "1" : "2") + "\n";
	}
	prompt += "Current phase: ";
	if (current_phase == PlayerTurn) { // current_phase が enum PlayerTurn と比較可能であることを前提
		prompt += "PlayerTurn";
	} else if (current_phase == EnemyTurn) { // current_phase が enum EnemyTurn と比較可能であることを前提
		prompt += "EnemyTurn";
	} else {
		prompt += "UnknownPhase"; // 予期せぬフェーズの場合のフォールバック
	}
	prompt += "\n";

	// 行動するユニットの可能なアクションをプロンプトに含める
	prompt += "\nAll map positions (X, Y) are **strictly 0-indexed**, where (0,0) is the top-left corner. **DO NOT add 1 or any offset to these coordinates.**\n"; // 座標の厳密な定義とオフセットの禁止

	prompt += "\nActions for " + acting_unit.name + " (from current position):\n";

	// 可能な移動位置を計算し、プロンプトに追加
	prompt += "  Possible MOVE locations (x,y): ";
	std::set<std::pair<int, int>> move_range = get_move_range(acting_unit);
	bool first_move_loc = true;
	if (acting_unit.has_moved || move_range.empty()) {
		prompt += "None (already moved or no valid moves)";
	} else {
		for (const auto& pos : move_range) {
			if (!first_move_loc) prompt += ", ";
			prompt += "(" + std::to_string(pos.first) + "," + std::to_string(pos.second) + ")";
			first_move_loc = false;
		}
	}
	prompt += "\n";

	prompt += "When choosing a MOVE action:\n";
	prompt += "  - You **MUST** select a 'target_x' and 'target_y' that is **EXACTLY** from the 'Possible MOVE locations' list. This list contains only valid and unoccupied cells reachable by your unit.\n"; // リストからの厳密な選択を再度強調
	prompt += "  - The chosen target position **MUST be DIFFERENT from the unit's current position** and **MUST NOT be occupied by any other unit.** Moving to an occupied cell or your current cell is invalid and will fail.\n"; // 占有禁止と現在地禁止を明確に

	// 攻撃可能なターゲットを計算し、プロンプトに追加
	prompt += "  Possible ATTACK targets (unit_name): ";
	std::set<std::pair<int, int>> attack_range_tiles = get_attack_range(acting_unit);
	std::vector<std::string> attackable_unit_names;
	for (const auto& u : units) { // グローバル変数 'units' を使用
		if (!u.is_enemy && u.hp > 0) { // 敵ではない（味方）ユニットで、HPが0より大きい
			// 攻撃射程内にいるか確認
			if (attack_range_tiles.count({ u.x, u.y })) {
				attackable_unit_names.push_back(u.name);
			}
		}
	}
	bool first_attack_target = true;
	if (acting_unit.has_attacked || attackable_unit_names.empty()) {
		prompt += "None (already attacked or no valid targets)";
	} else {
		for (const auto& target_name : attackable_unit_names) {
			if (!first_attack_target) prompt += ", ";
			prompt += target_name;
			first_attack_target = false;
		}
	}
	prompt += "\n";

	prompt += "\nYour task is to provide the next action(s) for " + acting_unit.name + " to **aggressively and strategically defeat ally units**. Your goal is their complete elimination.\n"; // より攻撃的な目標を明示
	prompt += "A unit can perform up to two actions in a turn: one MOVE and one ATTACK. You can choose to perform these actions in any order, or only one action. If you perform a MOVE, the ATTACK (if any) will be from the unit's new position after moving.\n";

	prompt += "**Strategic Action Guidelines (Prioritized):**\n"; // 優先順位を強調
	prompt += "1. **ABSOLUTE PRIORITY: ATTACK IF POSSIBLE.** If *any* ally unit is within your attack range (from your current position or a reachable move position), you **MUST** prioritize attacking them. Choose the target that can be defeated or deals the most damage. **Do NOT move if it prevents an immediate attack.**\n"; // 攻撃の最優先を強調
	prompt += "2. **INITIATE COMBAT:** If no ally is currently in your attack range:\n";
	prompt += "   - **Maximize your movement to get into attack range of an ally in THIS turn.** If you cannot attack this turn, then **maximize your movement to get into attack range for the NEXT turn.**\n"; // 攻撃範囲への移動の最大化を強調
	prompt += "   - When moving, evaluate all 'Possible MOVE locations'. Select the one that **most effectively closes the distance to the closest enemy**, ensuring you can attack this turn or next.\n";
	prompt += "   - **Additionally, consider cutting off escape routes or cornering enemy units if it leads to a quicker defeat.**\n"; // 追撃と囲い込みを促進
	prompt += "   - **Avoid moving into an enemy's attack range if you cannot attack them this turn.**\n";
	prompt += "3. **OPTIMAL ENGAGEMENT:**\n";
	prompt += "   - If an ally is in your attack range and you have moves remaining after attacking, consider moving to a safer position *only if* it doesn't prevent you from attacking a new target next turn or puts you out of harm's way significantly. **Ensure your chosen move maintains offensive pressure.**\n"; // 攻撃後の移動に関するガイダンス
	
	prompt += "4. **ACTION IMPERATIVE: Always attempt to perform an action (MOVE or ATTACK) if a valid and strategically beneficial one exists.** Only skip your turn (perform no actions) as a last resort, when absolutely no beneficial MOVE or ATTACK is possible and waiting is the only option.\n"; // 行動の強制と待機（スキップ）の抑制

	prompt += "You must only choose actions that are valid based on the unit's current state and rules.\n";
	prompt += "Specifically, for MOVE actions, the **'target_x' and 'target_y' MUST be chosen DIRECTLY from the provided 'Possible MOVE locations' list. DO NOT generate new coordinates not in that list.**\n"; // リストからの直接選択を強制
	prompt += "Format your response as a **SINGLE JSON array** of action objects. Do NOT include any text or commas outside this single array. Your response must start with '[' and end with ']'.\n"; // 「SINGLE JSON array」と外部カンマ禁止を再強調

	prompt += "Example single MOVE: [{\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"MOVE\", \"target_x\": X, \"target_y\": Y}]\n";
	prompt += "Example single ATTACK: [{\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"ATTACK\", \"target_unit_name\": \"TARGET_NAME\"}]\n";
	prompt += "Example ATTACK then MOVE: [{\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"ATTACK\", \"target_unit_name\": \"TARGET_NAME\"}, {\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"MOVE\", \"target_x\": X, \"target_y\": Y}]\n";
	prompt += "Example MOVE then ATTACK: [{\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"MOVE\", \"target_x\": X, \"target_y\": Y}, {\"unit_name\": \"" + acting_unit.name + "\", \"action_type\": \"ATTACK\", \"target_unit_name\": \"TARGET_NAME\"}]\n";
	prompt += "Only provide the JSON array. Do not include any other text, comments, or explanations whatsoever. Your response must start with '[' and end with ']'.\n";

	return prompt;
}

// 文字列の前後の空白文字をトリムするヘルパー関数
std::string trim_whitespace(const std::string& str) {
	size_t first = str.find_first_not_of(" \t\n\r\f\v");
	if (std::string::npos == first) {
		return str; // 空白のみの文字列
	}
	size_t last = str.find_last_not_of(" \t\n\r\f\v");
	return str.substr(first, (last - first + 1));
}

// Ollamaにコマンドをリクエストし、応答を処理する関数
std::string requestOllamaCommand(const Unit& acting_unit) {
	std::string game_state_prompt = getGameStateAsPrompt(acting_unit);

	// AIへのプロンプトをチャットログとゲームログに記録
	if (g_chatLogFile.is_open()) {
		g_chatLogFile << "--- AI Request ---\n" << game_state_prompt << "\n";
		g_chatLogFile.flush();
	}
	if (g_gameLogFile.is_open()) {
		nlohmann::json log_entry;
		log_entry["event"] = "AI_Prompt";
		log_entry["timestamp"] = getCurrentTimestamp();
		log_entry["prompt_content"] = game_state_prompt;
		g_gameLogFile << log_entry.dump() << "\n";
		g_gameLogFile.flush();
	}

	g_conversationHistory.push_back({ "user", game_state_prompt });

	OllamaChatRequest request_data;
	request_data.model = "phi3"; // 使用するモデル名
	request_data.messages = g_conversationHistory;
	request_data.stream = false;

	nlohmann::json json_request = request_data;
	std::string request_body = json_request.dump();

	std::string ai_response_content = "";

	if (g_ollamaClient) {
		auto res = g_ollamaClient->Post("/api/chat", request_body, "application/json");

		if (res && res->status == 200) {
			try {
				nlohmann::json json_response = nlohmann::json::parse(res->body);
				OllamaChatResponse response_data = json_response.get<OllamaChatResponse>();

				if (!response_data.message.content.empty()) {
					// AIの応答内容を ai_response_content に代入する
					ai_response_content = response_data.message.content;
					
					// AIの応答から余分なマークダウンや空白文字をトリムする
					// 前後の空白や改行を削除
					ai_response_content = trim_whitespace(ai_response_content);
					
					// '```json' と '```' を削除
					std::string json_start_marker = "```json";
					std::string json_end_marker = "```";
					
					size_t start_pos = ai_response_content.find(json_start_marker);
					if (start_pos != std::string::npos) {
						// 開始マーカーが見つかった場合
						ai_response_content.erase(start_pos, json_start_marker.length());

						// 終了マーカーを探す
						size_t end_pos = ai_response_content.find(json_end_marker);
						if (end_pos != std::string::npos) {
							ai_response_content.erase(end_pos, json_end_marker.length());
						}
					}
					// ここでもう一度トリムして、マークダウン削除後の余分な空白や改行を削除
					ai_response_content = trim_whitespace(ai_response_content);

					// AIが '[{"action1"}], [{"action2"}]' のように複数配列を生成するエラーを修正
					// '], [' を ', ' に置換し、単一の有効なJSON配列になるようにする
					std::string search_pattern = "], [";
					size_t found_pos = ai_response_content.find(search_pattern);
					if (found_pos != std::string::npos) {
						ai_response_content.replace(found_pos, search_pattern.length(), ", ");
						// 置換後に再度トリムして、余分な空白を削除
						ai_response_content = trim_whitespace(ai_response_content);
						log("Fixed malformed JSON: replaced '], [' with ', '."); // 修正をログに記録
					}

					// AIの応答をチャットログとゲームログに記録
					if (g_chatLogFile.is_open()) {
						g_chatLogFile << "--- AI Response ---\n" << ai_response_content << "\n";
						g_chatLogFile.flush();
					}
					if (g_gameLogFile.is_open()) {
						nlohmann::json log_entry;
						log_entry["event"] = "AI_Response";
						log_entry["timestamp"] = getCurrentTimestamp();
						log_entry["response_content"] = ai_response_content;
						g_gameLogFile << log_entry.dump() << "\n";
						g_gameLogFile.flush();
					}
					g_conversationHistory.push_back({ "assistant", ai_response_content });
				} else {
					std::cerr << "(AIからの応答がありませんでした)\n";
					if (g_chatLogFile.is_open()) g_chatLogFile << "(AIからの応答がありませんでした)\n";
				}
			} catch (const nlohmann::json::exception& e) {
				std::cerr << "JSONパースエラー: " << e.what() << " - 受信データ: " << res->body << std::endl;
				if (g_chatLogFile.is_open()) g_chatLogFile << "ERROR: JSON parse error: " << e.what() << "\n";
			}
		} else {
			std::cerr << "HTTPリクエストエラー: " << (res ? std::to_string(res->status) : "接続失敗") << std::endl;
			std::cerr << "Ollamaが起動しているか、またはAPIエンドポイントが正しいか確認してください。\n";
			if (g_chatLogFile.is_open()) g_chatLogFile << "ERROR: HTTP request error: " << (res ? std::to_string(res->status) : "connection failed") << "\n";
		}
	} else {
		std::cerr << "エラー: Ollamaクライアントが初期化されていません。\n";
	}
	return ai_response_content;
}

// エネミーターンのロジック
void enemy_turn_logic() {
	for (auto& enemy : units) {
		if (!enemy.is_enemy || enemy.hp <= 0) continue; // 敵ユニットかつ生存している場合のみ

		// AIからのコマンドをリクエスト
		log("Enemy Turn Starts! Requesting AI command...");
		std::string ai_command_json_str = requestOllamaCommand(enemy);

		if (ai_command_json_str.empty()) {
			log("not AI command");
			continue;
		}

		try {
			// ★ 変更: AIの応答をJSON配列としてパースする
			nlohmann::json ai_actions = nlohmann::json::parse(ai_command_json_str);

			if (!ai_actions.is_array()) {
				log(enemy.name + ": AI command was not a JSON array, skipping.");
				continue; // 配列でない場合はエラーとして次のユニットへ
			}

			// AIから指示された各アクションを順番に処理する
			for (const auto& action : ai_actions) {
				// 各アクションオブジェクトが必須キーを持っているか確認
				if (!action.contains("unit_name") || !action.contains("action_type")) {
					log(enemy.name + ": AI command action is missing required key, skipping.");
					continue; // 必須キーがなければスキップ
				}

				std::string unit_name = action.at("unit_name").get<std::string>();
				std::string action_type = action.at("action_type").get<std::string>();

				// コマンド対象のユニットが現在の敵ユニットと一致するか確認
				if (unit_name != enemy.name) {
					log(enemy.name + ": AI commanded unit name does not match current enemy unit: " + unit_name + ". Skip.");
					continue; // 別のユニットへの指示であればスキップ
				}

				if (action_type == "MOVE") {
					if (enemy.has_moved) {
						log(enemy.name + ": Skipping MOVE because already moved.");
						continue;
					}
					if (!action.contains("target_x") || !action.contains("target_y")) {
						log(enemy.name + ": Missing target_x/y in MOVE action. Skipping.");
						continue;
					}
					int target_x = action.at("target_x").get<int>();
					int target_y = action.at("target_y").get<int>();

					// 移動範囲のチェックと移動の実行
					std::set<std::pair<int, int>> move_range = get_move_range(enemy);
					if (move_range.count({ target_x, target_y }) && !is_occupied(target_x, target_y)) {
						enemy.x = target_x; // ユニットの実際の位置を更新
						enemy.y = target_y;
						enemy.has_moved = true; // 移動フラグを立てる
						log(enemy.name + " moved to (" + std::to_string(target_x) + ", " + std::to_string(target_y) + ")");
						// 移動後、enemyオブジェクトの位置が更新されるため、後続のATTACKアクションはこの新しい位置から計算される
					} else {
						log(enemy.name + " could not be moved to the specified position: (" + std::to_string(target_x) + ", " + std::to_string(target_y) + "). Skip.");
					}
				} else if (action_type == "ATTACK") {
					if (enemy.has_attacked) {
						log(enemy.name + ": Since an attack has already been performed, ATTACK is skipped.");
						continue;
					}
					if (!action.contains("target_unit_name")) {
						log(enemy.name + ": Missing target_unit_name in ATTACK action. Skip.");
						continue;
					}
					std::string target_unit_name = action.at("target_unit_name").get<std::string>();

					// ターゲットユニットを探す
					Unit* target_unit = nullptr;
					for (auto& ally : units) {
						// 敵ではない（味方）ユニットで、名前が一致し、かつHPが0より大きい（生存している）
						if (!ally.is_enemy && ally.name == target_unit_name && ally.hp > 0) {
							target_unit = &ally;
							break;
						}
					}

					if (target_unit) {
						// 攻撃範囲のチェックは、現在のユニット位置 (移動後であればその位置) から行われる
						std::set<std::pair<int, int>> attack_range = get_attack_range(enemy); // enemyの現在の位置 (移動後であればその位置) から計算
						if (attack_range.count({ target_unit->x, target_unit->y })) {
							attack(enemy, *target_unit); // 攻撃実行
							enemy.has_attacked = true; // 攻撃フラグを立てる
						} else {
							log(enemy.name + " failed to attack " + target_unit_name + " out of range, skipping.");
						}
					} else {
						log(enemy.name + " Units with specified " + target_unit_name + " Failed to attack: Target not found or not alive. Skipping.");
					}
				} else {
					log(enemy.name + ": Unknown AI action type: " + action_type + ". Skip.");
				}
			} // 各アクションのループ終了

		} catch (const nlohmann::json::exception& e) {
			log(enemy.name + ": JSON parsing error in AI command: " + std::string(e.what()) + " - Received Data: " + ai_command_json_str);
		} catch (const std::exception& e) {
			log(enemy.name + ": Unexpected error while processing AI command: " + std::string(e.what()));
		}
	}

	// 全てのユニットの行動フラグをリセットしてターン終了
	for (auto& u : units) {
		u.has_moved = false;
		u.has_attacked = false;
	}
	current_phase = PlayerTurn; // プレイヤーターンへ移行
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

	// --- LLMとロギング関連の初期化ここから ---
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
		std::cerr << "エラー: コンソール制御ハンドラの登録に失敗しました。\n";
	}
#endif

	const std::string LOG_FOLDER = "logs";
	if (!createDirectory(LOG_FOLDER)) {
		std::cerr << "ログフォルダの準備に失敗しました。ログは保存されない可能性があります。\n";
	}

	std::string chat_log_filepath = LOG_FOLDER + "/chat_log.txt";
	g_chatLogFile.open(chat_log_filepath, std::ios::app);
	if (!g_chatLogFile.is_open()) {
		std::cerr << "エラー: チャットログファイルを開けませんでした！\n";
	} else {
		g_chatLogFile << "\n--- 会話開始: " << getCurrentTimestamp() << " ---\n";
		g_chatLogFile.flush();
	}

	// デバッグ/リプレイログはゲームのセッションごとに新しいファイルを作成することを推奨
	std::string game_log_filepath = LOG_FOLDER + "/game_log_" + getCurrentTimestamp() + ".jsonl";
	g_gameLogFile.open(game_log_filepath, std::ios::app); // JSONL形式を想定
	if (!g_gameLogFile.is_open()) {
		std::cerr << "エラー: ゲームログファイルを開けませんでした！\n";
	} else {
		g_gameLogFile << "{\"event\": \"GameSessionStart\", \"timestamp\": \"" << getCurrentTimestamp() << "\"}\n";
		g_gameLogFile.flush();
	}

	// httplib::Client の初期化
	g_ollamaClient = new httplib::Client("http://localhost:11434");
	// --- LLMとロギング関連の初期化ここまで ---

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

		// TR1_LLM_SRPG用の設定
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

	// --- LLMとロギング関連の終了処理ここから ---
	if (g_chatLogFile.is_open()) {
		g_chatLogFile << "\n--- 会話終了 (通常終了) ---\n\n";
		g_chatLogFile.flush();
		g_chatLogFile.close();
	}
	if (g_gameLogFile.is_open()) {
		g_gameLogFile << "\n--- ゲームログ終了 (通常終了) ---\n\n";
		g_gameLogFile.flush();
		g_gameLogFile.close();
	}
	delete g_ollamaClient; // クライアントを解放
	g_ollamaClient = nullptr;
	// --- LLMとロギング関連の終了処理ここまで ---

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