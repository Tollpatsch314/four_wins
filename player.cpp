//#define DEBUG

#ifdef DEBUG
#define PSEUDO_GRAFIK_OUTPUT
#endif // DEBUG

#include <iostream>
#include <cstdint>
#include <climits>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <charconv>

#ifdef PSEUDO_GRAFIK_OUTPUT
#include <windows.h>
#endif // PSEUDO_GRAFIK_OUTPUT

#define DEPHT 5
#define SELF 0b11
#define VECTOR_AT(vector, num) ((vector >> num) & 0b11)


auto get_game_master_respone(const std::string& path) noexcept -> std::string {
	std::ifstream file_handle;
	std::string file_contents;
	do {
		file_handle.open(path);
		file_handle >> file_contents;
		file_handle.close();
	} while (file_contents.empty() or file_contents[0] == '>');

	return file_contents.substr(1, file_contents.size());
}

void trunc_file(const std::string& path) {
	std::ofstream file_handle(path, std::ios::trunc);
	file_handle.close();
}

void write_response(const std::string& path, unsigned short value) noexcept {
	std::ofstream file_handle(path, std::ios::trunc);
	file_handle << ">" + std::to_string(value);
	file_handle.close();
}

// returns:
//   - (0x00) if nobody won
//   - (0x01) if Player 1 won (0b01)
//   - (0x03) if Player 2 won (0b11)
inline uint8_t has_won(uint32_t field[4]) {

	uint32_t tmp, tmp1, tmp2;
	uint8_t type = 0;

	for (type = 0; type < 4; type++) {
		// diagonal
		if ((field[type] & 0xC030'0C03) == 0xC030'0C03 || (field[type] & 0x030C'30C0) == 0x030C'30C0) goto won_01;
		if ((field[type] & 0xC030'0C03) == 0x4010'0401 || (field[type] & 0x030C'30C0) == 0x0104'1040) goto won_11;
		for (uint8_t t = 0; t < 4; t++) {
			// vertical (x)
			tmp = ((field[type] >> 8 * t) & 0xFF);
			if (tmp == 0x55) goto won_01;
			if (tmp == 0xFF) goto won_11;
			// horizontal (y)
			tmp = ((field[type] >> 2 * t) & 0x0303'0303);
			if (tmp == 0x0101'0101) goto won_01;
			if (tmp == 0x0303'0303) goto won_11;
		}
	}

	for (uint8_t type = 0; type < 0x0F; type++) {
		tmp = 0x03 << 2 * type;
		if (field[0] & tmp) {
			// "horizontal" (z)
			if ((field[0] & tmp) == (field[1] & tmp) &&
				(field[2] & tmp) == (field[3] & tmp) &&
				(field[0] & tmp) == (field[2] & tmp)) {
				if ((field[0] & tmp) == (0x01 << type)) goto won_01;
				if ((field[0] & tmp) == (0x03 << type)) goto won_11;
			}
		}
	}

	type ^= type;

	// the other diagonales
	for (uint8_t x = 0; x <= 0xF; x += 0x4) {
		for (int8_t mul = 0; mul <= 0x8; mul += 0x8) {
			for (int8_t direction = -1; direction < 2; direction++, direction++) {
				tmp2 = 0x01 << 2 * x, tmp1 = 0x03 << 2 * x;
				for (int8_t t = (direction < 0 ? 3 : 0); (direction < 0 ? t > 0 : t < 4); t += direction * mul) {
					tmp = field[t] & tmp1;
					if (tmp == tmp1) {
						if (type && type != 0b11) goto next;
						type = 0b11;
					}
					else if (tmp == tmp2) {
						if (type && type != 0b01) goto next;
						type = 0b01;
					}
					else goto next;
					tmp2 <<= 2, tmp1 <<= 2;
				}

				if (type == 0b11) goto won_11;
				else goto won_01;

			next:;
			}
		}
	}

	return 0;

won_01:
	return 0b01;
won_11:
	return 0b11;
}

inline bool addStone(uint32_t* field, uint8_t pos, uint8_t stone) {
	uint32_t set = stone << 2 * pos;
	for (uint8_t t = 0; t < 4; t++) {
		if (!(field[t] & set)) {
			field[t] |= set;
			return true;
		}
	}
	return false;
}

inline bool delStone(uint32_t* field, uint8_t pos) {
	uint32_t set = 0b11 << 2 * pos;
	for (int8_t t = 4; t >= 0; t--) {
		if (field[t] & set) {
			field[t] &= (~set);
			return true;
		}
	}
	return false;
}

int64_t eval(uint32_t field[4], uint8_t player, uint8_t winner) {
	if (winner) { 
		if (winner == SELF) return 0xFFFF'FFFF'F;
		return -0xFFFF'FFFF'F;
	}
	return 1;
}

inline uint32_t* clone_field(uint32_t field[4]) {
	uint32_t* tmp_field = new uint32_t[4];
	for (uint8_t t = 0; t < 4; t++) tmp_field[t] = field[t];
	return tmp_field;
}

int64_t minimax(uint32_t field[4], uint8_t player, int depht, int64_t alpha, int64_t beta, uint8_t* moves) {
	if(uint8_t winner = has_won(field) || depht == 0)
		return eval(field, player, winner);
	int max_value = alpha;
	uint32_t* tmp_field = clone_field(field);
	for (uint8_t t = 0; t < 0xF; t++) {
		if (addStone(tmp_field, t, player)) {
			int64_t value = -minimax(tmp_field, player ^ 0b10, depht - 1, -beta, -max_value, moves + 1);
			if (value > max_value) {
				max_value = value;
				if (depht == DEPHT) *moves = t;
				if (max_value >= beta) break;
			}
		}

		delStone(field, t);
	}
	delete tmp_field;

	return max_value;
}

inline void circulate(uint8_t* arr, size_t size) {
	for (uint8_t t = 1; t < size; t++) arr[t - 1] = arr[t];
	arr[size - 1] = 0;
}


inline void play(uint32_t field[4], uint8_t* moves, uint8_t player, size_t size) {
	for (size_t t = 0; t < size; t++, moves++, player ^= 0b10) addStone(field, *moves, player);
}


#ifdef PSEUDO_GRAFIK_OUTPUT
inline void CHANGE_RED(HANDLE current_hdl) {
	SetConsoleTextAttribute(current_hdl, 0x4 | 0x8);
}

inline void CHANGE_GREEN(HANDLE current_hdl) {
	SetConsoleTextAttribute(current_hdl, 0x2 | 0x8);
}

inline void CHANGE_STD(HANDLE current_hdl) {
	SetConsoleTextAttribute(current_hdl, 0x4 | 0x2 | 0x1);
}

inline void inverse(uint32_t* field) {
	uint32_t f[4] = {  };
	for (uint8_t t = 0, t2 = 3; t < 4; t++, t2--) f[t] = field[t2];
	for (uint8_t t = 0; t < 4; t++) field[t] = f[t];
}

void print(uint32_t field[4]) {
	for (uint8_t t = 0; t < 4; t++) std::cout << "[" << short(t) << "]: 0x" << std::hex << field[t] << "  ";
	std::cout << "\n";
	inverse(field);
	uint32_t tmp;
	HANDLE current_hdl = GetStdHandle(STD_OUTPUT_HANDLE);
	char c;

	for (uint8_t z = 0; z < 4; z++) {
		std::cout << "\n     ";
		for (uint8_t t = 0; t < 4; t++) {
			tmp = (field[0] >> (2 * t + z * 8)) & 0b11;
			if(tmp == 0b11) CHANGE_GREEN(current_hdl);
			else if (tmp) CHANGE_RED(current_hdl);
			std::cout << "\xB1\xB1\xB1\xB1";
			CHANGE_STD(current_hdl);
			std::cout << "        ";
		}
		std::cout << " \n";
		for (uint8_t x0 = 0, x1 = 4; x0 < 4; x0++, x1--) {
			for (uint8_t t = 0; t < x1; t++) std::cout << " ";
			for (uint8_t t = 0; t < 4; t++) {
				if (x0 % 2) c = '\xB1';
				else c = '\xDB';

				if (x0 < 3) {
					tmp = (field[x0 + 1] >> (2 * t + z * 8)) & 0b11;
					if (tmp == 0b11) CHANGE_GREEN(current_hdl);
					else if (tmp) CHANGE_RED(current_hdl);
					std::cout << c;
					CHANGE_STD(current_hdl);
				}
				else std::cout << " ";

				tmp = (field[x0] >> (2 * t + z * 8)) & 0b11;

				if (x0 % 2) c = '\xDB';
				else c = '\xB1';

				if (tmp == 0b11) {
					CHANGE_GREEN(current_hdl);
					std::cout << c << c << c << c;
					CHANGE_STD(current_hdl);
					//std::cout << "       ";
				}
				else if (tmp) {
					CHANGE_RED(current_hdl);
					std::cout << c << c << c << c;
					CHANGE_STD(current_hdl);
					//std::cout << "       ";
				}
				else {
					std::cout << c << c << c << c;// << "      ";
				}

				/*if (t < 3) {
					for (uint8_t t2 = 0; t2 < x0; t2++) std::cout << ' ';
					std::cout << "\xB0\xB0";
					for (uint8_t t2 = x0; t2 < 5; t2++) std::cout << ' ';
				}*/

				std::cout << "       ";
			}
			std::cout << "\n";
		}
		//std::cout << "         \xB0\xB0          \xB0\xB0          \xB0\xB0";
	}

	std::cout << std::endl;
	inverse(field);
}
#endif // PSEUDO_GRAFIK_OUTPUT

int main(int argc, char** argv)
{
	using namespace std::chrono_literals;

	char* player = argv[1];
	std::string file = std::string(argv[2]);
	std::cout << "Starting as " << player << " Player.\nWriting into file: \"" << file << "\"" << std::endl;

	size_t number;
	uint8_t moves[DEPHT] = {};
	uint32_t field[4] = {};

	while (true) {
		auto input = get_game_master_respone(file);
		trunc_file(file);
		if (input == "end") break;
		else if (input != "start") {
			std::cout << "input: " << input;
			auto result = std::from_chars(input.data(), input.data() + input.size(), number);
			if (result.ec == std::errc::invalid_argument) {
				std::cerr << "Could not convert.";
				break;
			}
			
			addStone(field, number, 0b01);
			/*if ((uint8_t)number == *moves) {
				uint32_t* tmp_field = clone_field(field);
				circulate(moves, 2 * DEPHT);
				play(tmp_field, moves, 0b11, DEPHT);
				minimax(tmp_field, 0b11, DEPHT, LLONG_MIN, LLONG_MAX, (moves + DEPHT));
				delete tmp_field;
				goto next;
			}
			else */
		}

		minimax(field, 0b11, DEPHT, LLONG_MIN, LLONG_MAX, moves);
		addStone(field, *moves, 0b11);
		std::cout << "\nSelected Column: " << *moves << std::endl;
		write_response(file, *moves);
		//circulate(moves, DEPHT);
		for (uint8_t t = 0; t < DEPHT; t++) moves[t] = 0;

		std::this_thread::sleep_for(1s);
	}

	return 0;
}
