#include <SDL2/SDL.h>                 // SDL2
#include <SDL2/SDL_image.h>           // SDL2_image
#include <string>                     // 文字列を扱う
#include <vector>                     // 可変長配列
#include <iostream>                   // 標準出力と標準エラー出力
#include <tuple>                      // std::pairを使いたい
#include <utility>                    // pairから値を取得するstd::getを使いたい
#include <fstream>                    // ファイルを扱う
#include <sstream>                    // ファイルを文字列ストリームとして扱う
#include <optional>                   // 値が無い状態を扱えるstd::optionalを使いたい
#include <map>                        // 連想配列を使いたい
#include <functional>                 // 関数オブジェクトを使いたい
#include <boost/algorithm/string.hpp> // 文字列のsplitを使いたい

// ウィンドウサイズ
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

// コマンド名を数値にマッピングした型
enum CommandName {
  LABEL,
  IMAGE,
  CLEAR,
  TEXT,
  GOTO,
  SET,
  INPUT,
  IF,
  RETURN,
};

// コマンド名でCommandNameを引ける辞書として
const std::map<std::string, CommandName> COMMAND_MAP = {
  {"label", LABEL},
  {"image", IMAGE},
  {"clear", CLEAR},
  {"text", TEXT},
  {"goto", GOTO},
  {"set", SET},
  {"input", INPUT},
  {"if", IF},
  {"return", RETURN}
};

struct ImageState {
  SDL_Texture* tex;
  SDL_Rect rect;
};

struct EngineState {
  std::map<std::string, SDL_Texture*> textures;
  std::map<std::string, ImageState> draw_images;
};

using TypeName = std::string;
using Value = std::string;
using Parameter = std::pair<TypeName, Value>;
using Command = std::pair<CommandName, std::vector<Parameter>>;
using CommandFn = std::function<int(SDL_Renderer*, EngineState&, std::vector<Parameter>)>;

int command_nop(SDL_Renderer* renderer, EngineState& state, std::vector<Parameter> params) {
  return 0;
}

static std::map<CommandName, CommandFn> COMMAND_FN_MAP = {
  {LABEL, command_nop},
  {IMAGE, command_nop},
  {TEXT, command_nop},
  {GOTO, command_nop},
  {SET, command_nop},
  {INPUT, command_nop},
  {IF, command_nop},
  {RETURN, command_nop}
};

// ファイルの存在を確認する
inline bool exists_file (const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

// ファイルから文字列を読み込む
std::optional<std::string>
load_txt(const std::string pathstr)
{
  std::ifstream ifs(pathstr);

  if (!ifs)
    return std::nullopt;

  std::stringstream ss;
  ss << ifs.rdbuf();
  ifs.close();

  std::string data(ss.str());

  return data;
}

// パラメータ位置にある文字列を解釈してParameter型に変換
Parameter parse_param(const std::string& source) {
	Parameter result;
	bool is_a_number = false;
    try
    {
      // 文字列を数値(浮動小数点数)だとみなして解釈を試みる
      std::stod(source);
      is_a_number = true;
      result = std::make_pair("NUMBER", source);
    }
    catch(const std::exception &)
    {
      // 失敗したらダブルクォーテーションで囲まれているかどうかで解釈を変える
      if (source.front() == '\"' && source.back() == '\"') {
        result = std::make_pair("STRING", source.substr(1, source.size()-2));
      } else {
        // 上記のいずれでも無い場合はシンボルとして解釈
        result = std::make_pair("SYMBOL", source);
      }
    }
	return result;
}

// スクリプト文字列を解釈してコマンド列に変換
std::vector<Command>
parse(const std::string& source)
{
  std::vector<Command> result;
  std::vector<std::string> lines;

  // スクリプト文字列を改行文字で区切る
  boost::split(lines, source, boost::is_any_of("\n"));

  // 行単位で処理
  for (auto line : lines) {
    // 空行は飛ばす
    if (line == "") continue;

    std::vector<std::string> tokens;
    // 行をタブ文字で区切る
    boost::split(tokens, line, boost::is_any_of("\t"));

    CommandName name;
    // 行頭の文字列をコマンド名として解釈
    if (COMMAND_MAP.find(tokens[0]) != COMMAND_MAP.end()) {
      name = COMMAND_MAP.at(tokens[0]);
    } else {
      // 予約済みのコマンド名と一致しなかったらエラー出力(その行は無視する)
      std::cerr << "Invalid command name: '" << tokens[0] << "'" << std::endl;
      continue;
    }

    std::vector<Parameter> parameters;
    // 引数位置に文字列が存在する場合は1つずつParameterとして解釈
    if (tokens.size() > 1) {
      for (auto iter = tokens.begin()+1; iter != tokens.end(); iter++) {
        Parameter param = parse_param(*iter);
        parameters.push_back(param);
      }
    }

    result.push_back(make_pair(name, parameters));
  }

  return result;
}

// 画像を表示するコマンド
int command_image(SDL_Renderer* renderer, EngineState& state, std::vector<Parameter> params) {
  std::string id = std::get<1>(params[0]);
  std::string img_path = std::get<1>(params[1]);
  
  if (state.textures.find(img_path) == state.textures.end()) {
    std::cerr << "'image': " << img_path << " is not loaded." << std::endl;
    return 1;
  }

  SDL_Texture* tex = state.textures.at(img_path);
  SDL_Rect rect;
  rect.x = std::stoi(std::get<1>(params[2]));
  rect.y = std::stoi(std::get<1>(params[3]));
  rect.w = std::stoi(std::get<1>(params[4]));
  rect.h = std::stoi(std::get<1>(params[5]));

  ImageState imgst{tex, rect};
  state.draw_images[id] = imgst;
  return 0;
}

int
main(int argc, char* args[])
{
  SDL_Window* window = NULL;
  EngineState state{};
  SDL_Surface* screenSurface = NULL;
  SDL_Renderer* renderer;
  // 現在処理中のコマンドのインデックス
  int command_index = 0;

  // スクリプト文字列をファイルから読み込む
  std::string source = load_txt("./script").value();

  // スクリプト文字列を解釈してコマンド列に変換
  auto commands = parse(source);

  // コマンド列を出力(デバッグ用)
  for (auto cmd : commands) {
    std::cout << std::get<0>(cmd) << ":";
    for (auto param : std::get<1>(cmd)) {
      std::cout << "<" << std::get<0>(param) << ">" << std::get<1>(param) << " ";
    }
	  std::cout << std::endl;
  }

  // コマンドに対応する関数を登録
  COMMAND_FN_MAP[IMAGE] = command_image;

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
    std::exit(1);
  }

    // Create window
  window = SDL_CreateWindow("げーむえんじん",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            SCREEN_WIDTH,
                            SCREEN_HEIGHT,
                            SDL_WINDOW_SHOWN);
  if (window == NULL) {
    std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
    std::exit(1);
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  // スクリプト中の画像を事前にロードしておく
  for (auto cmd: commands) {
    // imageコマンドである
    if (std::get<0>(cmd) == IMAGE) {
      auto params = std::get<1>(cmd);
      // 第2引数がある
      if (params.size() < 2) {
        std::cerr << "'image' command should have a file path at 2nd argument." << std::endl;
        continue;
      }
      auto p2 = params[1];
      // 第2引数の型がSTRINGである
      if (std::get<0>(p2) != "STRING") {
        std::cerr << "2nd argument of 'image' command should be a string." << std::endl;
        continue;
      }
      std::string image_path = std::get<1>(p2);
      // 第2引数で指定したファイルが存在する
      if (!exists_file(image_path)) {
        std::cerr << "file: " << image_path << " not found." << std::endl;
        continue;
      }
      // 画像ファイルを読み込んでテクスチャにする
      SDL_Texture* tex = IMG_LoadTexture(renderer, image_path.c_str());
      // 画像パスとテクスチャを紐付けておく
      state.textures[image_path] = tex;
    }
  }

  std::cout << "Textures are cached." << std::endl;

  // Get window surface
  screenSurface = SDL_GetWindowSurface(window);
  
  while (1) {
    SDL_Event e;
    if (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        break;
      }
    }

    SDL_RenderClear(renderer);

    // 0. 未処理のコマンドがあるかチェック
    if (command_index < commands.size()) {
      // 1. 次のコマンドを取得
      CommandName command_name;
      std::vector<Parameter> params;
      std::tie(command_name, params) = commands[command_index];
      // 2. コマンド種類に応じて処理分け
      COMMAND_FN_MAP[command_name](renderer, state, params);
      command_index += 1;
    }

    // 表示中の画像を描画
    for (auto iter = state.draw_images.begin(); iter != state.draw_images.end(); ++iter) {
      ImageState imgst = iter->second;
      SDL_RenderCopy(renderer, imgst.tex, NULL, &imgst.rect);
    }

    // 画面の表示を更新
    SDL_RenderPresent(renderer);

    // Wait two seconds
    SDL_Delay(13);
  }

  // 事前にロードしたテクスチャを解放
  for (auto iter = state.textures.begin(); iter != state.textures.end(); ++iter) {
    SDL_DestroyTexture(iter->second);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
