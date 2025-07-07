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

// ゲームの状態をAIプロンプトに変換する仮の関数
// 実際のゲームの状態に合わせて詳細に記述する必要があります
std::string getGameStateAsPrompt() {
	std::string prompt = "Current game state:\n";
	prompt += "Map size: " + std::to_string(MAP_SIZE) + "x" + std::to_string(MAP_SIZE) + "\n";
	prompt += "Map layout (0=PLAIN, 1=FOREST):\n";
	for (int y = 0; y < MAP_SIZE; ++y) {
		for (int x = 0; x < MAP_SIZE; ++x) {
			prompt += std::to_string(map[y][x]) + " ";
		}
		prompt += "\n";
	}
	prompt += "Units:\n";
	for (const auto& u : units) {
		prompt += "- " + u.name + (u.is_enemy ? " (Enemy)" : " (Ally)") +
			" Pos: (" + std::to_string(u.x) + "," + std::to_string(u.y) + ")" +
			" HP: " + std::to_string(u.hp) +
			" ATK: " + std::to_string(u.atk) +
			" DEF: " + std::to_string(u.def) +
			" Moved: " + (u.has_moved ? "Yes" : "No") +
			" Attacked: " + (u.has_attacked ? "Yes" : "No") +
			" Weapon: " + (u.weapon == WeaponType::Sword ? "Sword" : "Bow") + "\n";
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
	prompt += "Your task is to provide the next action for an enemy unit. Choose one enemy unit, and one action: MOVE or ATTACK.\n";
	prompt += "Format your response as a JSON object with 'unit_name', 'action_type', and 'target_x'/'target_y' or 'target_unit_name'.\n";
	prompt += "Example MOVE: {\"unit_name\": \"enemy1\", \"action_type\": \"MOVE\", \"target_x\": 7, \"target_y\": 4}\n";
	prompt += "Example ATTACK: {\"unit_name\": \"enemy1\", \"action_type\": \"ATTACK\", \"target_unit_name\": \"ally1\"}\n";
	prompt += "Only provide the JSON object. Do not include any other text.\n";
	return prompt;
}

// Ollamaにコマンドをリクエストし、応答を処理する関数
std::string requestOllamaCommand() {
	std::string game_state_prompt = getGameStateAsPrompt();

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
					ai_response_content = response_data.message.content;
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
	log("Enemy Turn Starts! Requesting AI command...");
	std::string ai_command_json_str = requestOllamaCommand();

	if (!ai_command_json_str.empty()) {
		try {
			nlohmann::json ai_command = nlohmann::json::parse(ai_command_json_str);

			std::string unit_name = ai_command.at("unit_name").get<std::string>();
			std::string action_type = ai_command.at("action_type").get<std::string>();

			// AIが指示したユニットを見つける
			Unit* target_enemy_unit = nullptr;
			for (auto& u : units) {
				if (u.is_enemy && u.name == unit_name && u.hp > 0) {
					target_enemy_unit = &u;
					break;
				}
			}

			if (target_enemy_unit) {
				if (action_type == "MOVE" && !target_enemy_unit->has_moved) {
					int target_x = ai_command.at("target_x").get<int>();
					int target_y = ai_command.at("target_y").get<int>();

					// AIが指示した移動が有効かチェック
					std::set<std::pair<int, int>> possible_moves = get_move_range(*target_enemy_unit);
					if (possible_moves.count({ target_x, target_y }) && !is_occupied(target_x, target_y)) {
						target_enemy_unit->x = target_x;
						target_enemy_unit->y = target_y;
						target_enemy_unit->has_moved = true;
						log(target_enemy_unit->name + " Moved to (" + std::to_string(target_x) + ", " + std::to_string(target_y) + ")");
					} else {
						log("AI attempted invalid MOVE for " + target_enemy_unit->name + " to (" + std::to_string(target_x) + ", " + std::to_string(target_y) + ")");
					}
				} else if (action_type == "ATTACK" && !target_enemy_unit->has_attacked) {
					std::string target_unit_name = ai_command.at("target_unit_name").get<std::string>();

					// AIが指示したターゲットユニットを見つける
					Unit* target_ally_unit = nullptr;
					for (auto& u : units) {
						if (!u.is_enemy && u.name == target_unit_name && u.hp > 0) {
							target_ally_unit = &u;
							break;
						}
					}

					if (target_ally_unit) {
						// AIが指示した攻撃が有効かチェック
						std::set<std::pair<int, int>> attack_range = get_attack_range(*target_enemy_unit);
						if (attack_range.count({ target_ally_unit->x, target_ally_unit->y })) {
							attack(*target_enemy_unit, *target_ally_unit);
							target_enemy_unit->has_attacked = true;
						} else {
							log("AI attempted invalid ATTACK for " + target_enemy_unit->name + " on " + target_ally_unit->name + " (out of range)");
						}
					} else {
						log("AI attempted to ATTACK unknown/defeated unit: " + target_unit_name);
					}
				} else {
					log("AI provided invalid action type or unit already acted: " + action_type);
				}
			} else {
				log("AI commanded unknown/defeated enemy unit: " + unit_name);
			}

		} catch (const nlohmann::json::exception& e) {
			log("AI command JSON parse error: " + std::string(e.what()) + " - AI response: " + ai_command_json_str);
		} catch (const std::exception& e) {
			log("AI command processing error: " + std::string(e.what()));
		}
	} else {
		log("No AI command received.");
	}

	// すべての敵ユニットが行動を終えるまで、または適切なターン終了条件まで待つ
	// 今回はAIが1行動でターンを終える想定で簡略化
	for (auto& u : units) {
		if (u.is_enemy) { // 敵ユニットの状態をリセット
			u.has_moved = false;
			u.has_attacked = false;
		}
	}
	current_phase = PlayerTurn; // プレイヤーターンに戻す
	log("Enemy Turn Ends!");
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