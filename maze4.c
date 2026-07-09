// maze4_modified.c 
// 升级版迷宫：侧边栏布局 + AI光标(压迫感) + 右置信息 
// 修改说明： 
// 1. 移除了所有的WASD移动控制逻辑，仅保留方向键操作。 
// 2. 将main()函数模块化，拆分为init_game(), setup_windows(), game_loop(), cleanup()。 
// 3. 修复了光标显示优先级被覆盖的问题。
// 4. 修复了由于阻塞输入导致的计时器卡死问题。
// 5. 修复了UI动画和输入焦点的细节问题。
//
// 编译： gcc maze4_modified.c -o maze -lncurses -lm 
// 
// 操作： 
// 方向键 : 移动光标 
// z : 画墙 x : 画路 e : 橡皮 
// s : AI演示求解 
// u : 用户求解 
// c : 检查是否有解 
// i : 灵感点云 
// r : 重置 
// b : 保存 l : 读取 
// a : 人机赛跑 
// q : 退出 
// 
// 颜色：红黄=AI光标 品红=用户路径 青=AI路径 

#include <ncurses/ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>
#include <stdarg.h>

#define MAX_SIZE 100
#define SAVE_FILE "maze_save.bin"
#define TIMEOUT_SEC 60

const char *maze_art[] = {
"      ___           ___           ___           ___     ",
"     /\\  \\         /\\  \\         /\\__\\         /\\__\\    ",
"    |::\\  \\       /::\\  \\       /::|  |       /:/ _/_   ",
"    |:|:\\  \\     /:/\\:\\  \\     /:/:|  |      /:/ /\\__\\  ",
"  __|:|\\:\\  \\   /:/ /::\\  \\   /:/|:|  |__   /:/ /:/ _/_ ",
" /::::|_\\:\\__\\ /:/_/:/\\:\\__\\ /:/ |:| /\\__\\ /:/_/:/ /\\__\\",
" \\:\\~~\\  \\/__/ \\:\\/:/  \\/__/ \\/__|:|/:/  / \\:\\/:/ /:/  /",
"  \\:\\  \\        \\::/__/          |:/:/  /   \\::/_/:/  / ",
"   \\:\\  \\        \\:\\  \\          |::/  /     \\:\\/:/  /  ",
"    \\:\\__\\        \\:\\__\\         |:/  /       \\::/  /   ",
"     \\/__/         \\/__/         |/__/         \\/__/    ",
};


#pragma pack(push, 1)
typedef struct {
    int size;
    int maze[MAX_SIZE][MAX_SIZE];
    int cursor_r, cursor_c;
    double elapsed;
    int timer_running;
    int user_solving;
    int race_mode;
    int timed_out;
    int user_path[MAX_SIZE][MAX_SIZE];
    int visited[MAX_SIZE][MAX_SIZE];
    int path[MAX_SIZE][MAX_SIZE];
    int race_user_done;
    int race_algo_done;
} SaveData;
#pragma pack(pop)

typedef struct { int row, col; } Pos;

// ---------- 全局状态 ----------
int maze[MAX_SIZE][MAX_SIZE];
int size;
int visited[MAX_SIZE][MAX_SIZE];
int path[MAX_SIZE][MAX_SIZE];
int user_path[MAX_SIZE][MAX_SIZE];
int dr[] = {-1, 1, 0, 0};
int dc[] = {0, 0, -1, 1};

WINDOW *main_win;
WINDOW *info_win;

int cursor_r, cursor_c;
int ai_r = -1, ai_c = -1;
time_t start_time;
time_t race_start_time;
int timer_running = 0;
int race_mode = 0;
int race_user_done = 0;
int race_algo_done = 0;
int user_solving = 0;
int timed_out = 0;
int use_color = 0;
int current_stage = 0;
char last_msg[256] = "Welcome...";

// 新增：标记是否在启动时加载
int load_on_start = 0;

// 颜色定义
enum { C_WALL = 1, C_PATH, C_START, C_END, C_ALGO, C_USER, C_CURSOR, C_BG, C_DEAD, C_AI };

// ---------- 辅助函数 ----------
void print_right(WINDOW *w, int y, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    int max_x, max_y;
    getmaxyx(w, max_y, max_x);
    int x = max_x - strlen(buf) - 1;
    if (x < 0) x = 0;
    mvwprintw(w, y, x, "%s", buf);
}

// ---------- 初始化与设置 ----------
void init_colors_if_possible() {
    if (has_colors()) {
        start_color();
        use_color = 1;
        init_pair(C_WALL, COLOR_BLACK, COLOR_WHITE);
        init_pair(C_PATH, COLOR_BLACK, COLOR_BLUE);
        init_pair(C_START, COLOR_BLACK, COLOR_GREEN);
        init_pair(C_END, COLOR_BLACK, COLOR_RED);
        init_pair(C_ALGO, COLOR_BLACK, COLOR_CYAN);
        init_pair(C_USER, COLOR_BLACK, COLOR_MAGENTA);
        init_pair(C_CURSOR, COLOR_WHITE, COLOR_BLACK);
        init_pair(C_BG, COLOR_WHITE, COLOR_BLACK);
        init_pair(C_DEAD, COLOR_BLACK, COLOR_RED);
        init_pair(C_AI, COLOR_RED, COLOR_YELLOW);
    }
}

void init_maze() {
    for (int i = 0; i < size; i++)
        for (int j = 0; j < size; j++)
            maze[i][j] = 1;
    for (int i = 1; i < size-1; i++)
        for (int j = 1; j < size-1; j++)
            maze[i][j] = 0;
    maze[1][1] = 0;
    maze[size-2][size-2] = 0;
}

void reset_paths() {
    memset(path, 0, sizeof(path));
    memset(user_path, 0, sizeof(user_path));
    memset(visited, 0, sizeof(visited));
    ai_r = -1; ai_c = -1;
}

double elapsed_seconds() {
    if (!timer_running) return 0;
    return difftime(time(NULL), start_time);
}

// 模块化：启动菜单与环境初始化
void init_game() {
    srand((unsigned)time(NULL));
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colors_if_possible();

    int choice = 0;
    
    // 循环菜单，直到做出有效选择
    while (1) {
        clear();
        int h = 29; // 增加高度以容纳详细说明
        int w = 60;
        int y = (LINES - h) / 2;
        int x = (COLS - w) / 2;
        if (y < 0) y = 0;
        
        WINDOW *menu_win = newwin(h, w, y, x);
        box(menu_win, 0, 0);
        keypad(menu_win, TRUE);

        mvwprintw(menu_win, 1, 2, maze_art[0]);
        mvwprintw(menu_win, 2, 2, maze_art[1]);
        mvwprintw(menu_win, 3, 2, maze_art[2]);
        mvwprintw(menu_win, 4, 2, maze_art[3]);
        mvwprintw(menu_win, 5, 2, maze_art[4]);
        mvwprintw(menu_win, 6, 2, maze_art[5]);
        mvwprintw(menu_win, 7, 2, maze_art[6]);
        mvwprintw(menu_win, 8, 2, maze_art[7]);
        mvwprintw(menu_win, 9, 2, maze_art[8]);
        mvwprintw(menu_win, 10, 2, maze_art[9]);
        mvwprintw(menu_win, 11, 2, maze_art[10]);

        
        // 显示选项
        mvwprintw(menu_win, 14, 4, "[N] New Game");
        mvwprintw(menu_win, 15, 4, "[L] Load Game from '%s'", SAVE_FILE);
        
        // 详细操作说明
        mvwprintw(menu_win, 16, 2, "--- Controls Guide ---");
        mvwprintw(menu_win, 17, 4, "Arrows: Move Cursor");
        mvwprintw(menu_win, 18, 4, "z: Draw Wall   x: Draw Path");
        mvwprintw(menu_win, 19, 4, "s: AI Auto-Solve with demo animation");
        mvwprintw(menu_win, 20, 4, "u: User Manual Solve and timer starts");
        mvwprintw(menu_win, 21, 4, "c: Check if maze has solution");
        mvwprintw(menu_win, 22, 4, "i: Sprinkle Inspiration (Random walls/paths)");
        mvwprintw(menu_win, 23, 4, "a: Race Mode (User vs AI)");
        mvwprintw(menu_win, 24, 4, "r: Reset   b: Save   l: Load   q: Quit");

        if (choice == -1) {
            mvwprintw(menu_win, 16, 4, "Error: No save file found or invalid file!");
        }

        wrefresh(menu_win);
        
        int ch = wgetch(menu_win);
        
        if (ch == 'n' || ch == 'N') {
            // 新游戏：输入大小
            delwin(menu_win);
            clear();
            refresh();
            
            WINDOW *input_win = newwin(5, 40, (LINES-5)/2, (COLS-40)/2);
            box(input_win, 0, 0);
            mvwprintw(input_win, 1, 2, "=== New Game ===");
            mvwprintw(input_win, 2, 2, "Enter Maze Size (3-50): ");
            wrefresh(input_win);
            echo();
            curs_set(1);
            int sz = 0;
            mvwscanw(input_win, 2, 26, "%d", &sz);
            curs_set(0);
            noecho();
            delwin(input_win);
            
            if (sz < 3) sz = 3;
            if (sz > 50) sz = 50;
            size = sz + 2;
            if (size > MAX_SIZE) size = MAX_SIZE;
            
            load_on_start = 0;
            break; // 退出菜单循环
        } 
        else if (ch == 'l' || ch == 'L') {
            // 读取存档：预读大小
            FILE *fp = fopen(SAVE_FILE, "rb");
            if (fp) {
                SaveData header;
                // 尝试读取 size
                if (fread(&header, sizeof(SaveData), 1, fp)) {
                    // 校验大小有效性
                    if (header.size >= 5 && header.size <= MAX_SIZE) {
                        size = header.size;
                        load_on_start = 1;
                        fclose(fp);
                        delwin(menu_win);
                        break; // 退出菜单循环
                    }
                }
                fclose(fp);
            }
            // 如果失败，标记错误并继续循环
            choice = -1;
            delwin(menu_win);
        } 
        else if (ch == 'q' || ch == 'Q') {
            endwin();
            exit(0);
        }
    }
    clear();
    refresh();
}

void setup_windows() {
    int maze_h = size + 2;
    int maze_w = size * 2 + 2;
    int info_w = 32;
    int total_w = maze_w + info_w + 2;
    int start_y = 2;
    int start_x = (COLS - total_w) / 2;
    if (start_x < 1) start_x = 1;
    main_win = newwin(maze_h, maze_w, start_y, start_x);
    info_win = newwin(maze_h, info_w, start_y, start_x + maze_w + 1);
    keypad(main_win, TRUE);
}

void cleanup() {
    delwin(main_win);
    delwin(info_win);
    endwin();
}

// ---------- 绘制核心 ----------
void draw_maze() {
    werase(main_win);
    box(main_win, 0, 0);
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int y = i+1, x = j*2+1;
            chtype ch = ' ';
            int color = C_PATH;
            int is_start = (i == 1 && j == 1);
            int is_end = (i == size-2 && j == size-2);
            int is_user = (i == cursor_r && j == cursor_c);
            int is_ai = (i == ai_r && j == ai_c && ai_r != -1);

            if (use_color) {
                if (is_ai) { color = C_AI; ch = 'O'; }
                else if (is_user) { color = C_CURSOR; ch = 'X'; }
                else if (is_start) { color = C_START; ch = 'S'; }
                else if (is_end) { color = C_END; ch = 'E'; }
                else if (user_path[i][j]) { color = C_USER; ch = ' '; }
                else if (path[i][j]) { color = C_ALGO; ch = ' '; }
                else if (visited[i][j] == 2) { color = C_DEAD; ch = ' '; }
                else if (maze[i][j] == 1) { color = C_WALL; ch = ' '; }
                else { color = C_PATH; ch = ' '; }

                wbkgdset(main_win, COLOR_PAIR(color));
                mvwaddch(main_win, y, x, ch);
                mvwaddch(main_win, y, x+1, ' ');
                wbkgdset(main_win, COLOR_PAIR(C_BG));
            } else {
                if (is_ai) ch = 'O';
                else if (is_user) ch = 'X';
                else if (is_start) ch = 'S';
                else if (is_end) ch = 'E';
                else if (user_path[i][j]) ch = '@';
                else if (path[i][j]) ch = '*';
                else if (visited[i][j] == 2) ch = ' ';
                else if (maze[i][j] == 1) ch = '#';
                else ch = '.';
                mvwaddch(main_win, y, x, ch);
                mvwaddch(main_win, y, x+1, ' ');
            }
        }
    }
    wrefresh(main_win);
}

// ---------- 信息栏 ----------
void set_stage(int new_stage) {
    int max_w = getmaxx(info_win) - 2;
    int start_w = current_stage * max_w / 3;
    int end_w = new_stage * max_w / 3;
    if (start_w == end_w) { current_stage = new_stage; return; }
    int step = (start_w < end_w) ? 1 : -1;
    for (int i = start_w; i != end_w; i += step) {
        mvwhline(info_win, 8, 1, ACS_CKBOARD, i);
        mvwhline(info_win, 8, i + 1, ' ', max_w - i);
        wrefresh(info_win);
        napms(10);
    }
    current_stage = new_stage;
}

void show_info(const char *msg) {
    werase(info_win);
    box(info_win, 0, 0);
    if (msg && *msg) strncpy(last_msg, msg, sizeof(last_msg)-1);
    else if (last_msg[0] == '\0') strcpy(last_msg, "Ready...");

    double el = elapsed_seconds();
    int mm = (int)el / 60;
    int ss = (int)el % 60;

    char mode_str[32] = "EDIT";
    if (user_solving) strcpy(mode_str, "USER SOLVE");
    else if (race_mode) strcpy(mode_str, "RACE MODE");
    else if (timed_out) strcpy(mode_str, "TIMEOUT");

    print_right(info_win, 1, "Time: %02d:%02d", mm, ss);
    print_right(info_win, 2, "Mode: %s", mode_str);
    print_right(info_win, 3, "Cursor: %d,%d", cursor_r, cursor_c);

    mvwhline(info_win, 4, 1, ACS_HLINE, getmaxx(info_win)-2);
    print_right(info_win, 5, "%s", last_msg);
    mvwhline(info_win, 6, 1, ACS_HLINE, getmaxx(info_win)-2);

    mvwprintw(info_win, 7, 1, "Game Progress:");
    int max_w = getmaxx(info_win) - 2;
    int bar_w = current_stage * max_w / 3;
    for (int i = 1; i <= bar_w; i++) mvwaddch(info_win, 8, i, ACS_CKBOARD);
    mvwprintw(info_win, 9, 1, "1.Draw 2.Check 3.Play");

    if (race_mode || user_solving) {
        int time_left = TIMEOUT_SEC - (int)el;
        if (time_left < 0) time_left = 0;
        int race_bar_w = time_left * max_w / TIMEOUT_SEC;
        mvwprintw(info_win, 11, 1, "Race Timeout:");
        for (int i = 1; i <= race_bar_w; i++) mvwaddch(info_win, 12, i, ACS_CKBOARD | COLOR_PAIR(C_DEAD));
    }

    mvwprintw(info_win, getmaxy(info_win)-4, 1, "[1]z/x:Edit [2]u:Manual");
    mvwprintw(info_win, getmaxy(info_win)-3, 1, "[3]s:AI [4]a:Race");
    mvwprintw(info_win, getmaxy(info_win)-2, 1, "[5]b:Save [6]l:Load");
    mvwprintw(info_win, getmaxy(info_win)-1, 1, "[7]i:Hint [8]c:Check [9]r:Reset");
    wrefresh(info_win);
}

void check_timeout() {
    if (!timer_running || timed_out) return;
    if (elapsed_seconds() >= TIMEOUT_SEC) {
        timed_out = 1;
        show_info("Timeout! AI taking over...");
    }
}

// ---------- 核心算法 ----------
int solve_maze_internal(int animate) {
    reset_paths();
    Pos stack[MAX_SIZE*MAX_SIZE];
    int top = -1;
    stack[++top] = (Pos){1, 1};
    visited[1][1] = 1;
    ai_r = 1; ai_c = 1;
    while (top >= 0) {
        Pos cur = stack[top];
        int r = cur.row, c = cur.col;
        ai_r = r; ai_c = c;
        if (r == size-2 && c == size-2) {
            for (int i = 0; i <= top; i++) path[stack[i].row][stack[i].col] = 1;
            if (animate) { draw_maze(); show_info("Solved!"); }
            return 1;
        }
        int found = 0;
        for (int d = 0; d < 4; d++) {
            int nr = r + dr[d];
            int nc = c + dc[d];
            if (nr >= 0 && nr < size && nc >= 0 && nc < size && maze[nr][nc] == 0 && visited[nr][nc] == 0) {
                visited[nr][nc] = 1;
                stack[++top] = (Pos){nr, nc};
                found = 1;
                if (animate) { draw_maze(); show_info("AI Searching..."); napms(30); }
                break;
            }
        }
        if (!found) {
            visited[r][c] = 2;
            top--;
            if (animate) { draw_maze(); show_info("AI Backtracking..."); napms(20); }
        }
    }
    return 0;
}

int maze_solvable() {
    static int v_backup[MAX_SIZE][MAX_SIZE];
    static int p_backup[MAX_SIZE][MAX_SIZE];
    memcpy(v_backup, visited, sizeof(visited));
    memcpy(p_backup, path, sizeof(path));
    reset_paths();
    int r = solve_maze_internal(0);
    memcpy(visited, v_backup, sizeof(visited));
    memcpy(path, p_backup, sizeof(path));
    return r;
}

int user_solve_mode() {
    if (!maze_solvable()) {
        show_info("No solution.");
        napms(500);
        show_info(" ");
        napms(500);
        show_info("No solution.");
        return 0; 
        }
    user_solving = 1;
    reset_paths();
    cursor_r = 1; cursor_c = 1;
    user_path[1][1] = 1;
    timer_running = 1;
    start_time = time(NULL);
    draw_maze();
    show_info("Manual Mode Started");
    napms(500); show_info(" "); napms(500); show_info("Manual Mode Started");
    return 1;
}

void sprinkle_inspiration() {
    wtimeout(main_win, -1);
    echo();
    mvwprintw(info_win, 5, 1, "Density (0-100): ");
    wrefresh(info_win);
    char buf[16] = {0};
    int n = 0, ch;
    while ((ch = wgetch(main_win)) != '\n' && n < 15) {
        if (ch >= '0' && ch <= '9') buf[n++] = ch;
    }
    buf[n] = 0;
    noecho();
    wtimeout(main_win, 100);
    int density = atoi(buf);
    if (density < 0) density = 0;
    if (density > 100) density = 100;
    int total = (size-2)*(size-2), count = total * density / 100;
    for (int k = 0; k < count; k++) {
        int r = 1 + rand() % (size-2);
        int c = 1 + rand() % (size-2);
        if ((r==1&&c==1) || (r==size-2&&c==size-2)) continue;
        maze[r][c] = (rand() % 2) ? 1 : 0;
    }
    reset_paths();
    draw_maze();
    show_info("Map Sprinkled");
    napms(500); show_info(" "); napms(500); show_info("Map Sprinkled");
}

void save_progress() {
    SaveData data;
    memset(&data, 0, sizeof(data));
    data.size = size;
    memcpy(data.maze, maze, sizeof(maze));
    data.cursor_r = cursor_r;
    data.cursor_c = cursor_c;
    data.elapsed = elapsed_seconds();
    data.timer_running = timer_running;
    data.user_solving = user_solving;
    data.race_mode = race_mode;
    data.timed_out = timed_out;
    memcpy(data.visited, visited, sizeof(visited));
    memcpy(data.path, path, sizeof(path));
    memcpy(data.user_path, user_path, sizeof(user_path));
    data.race_user_done = race_user_done;
    data.race_algo_done = race_algo_done;

    int fd = open(SAVE_FILE, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { show_info("Save Failed!"); return; }
    ssize_t written = write(fd, &data, sizeof(data));
    close(fd);
    if (written != sizeof(data)) { unlink(SAVE_FILE); show_info("Write Error!"); }
    else show_info("Saved Successfully");
}

int load_progress() {
    int fd = open(SAVE_FILE, O_RDONLY);
    if (fd < 0) {
        show_info("No Save File");
        napms(500); show_info(" "); napms(500); show_info("No Save File");
        return 0;
    }
    SaveData data;
    ssize_t n = read(fd, &data, sizeof(data));
    close(fd);

    // 检查大小是否合法且匹配当前窗口
    if (n != sizeof(data) || data.size < 5 || data.size > MAX_SIZE) {
        show_info("Corrupt Save");
        return 0;
    }
    
    // 检查是否在游戏运行中加载（此时size可能已改变）
    // 如果是在启动时加载，size已经在init_game设置好了
    if (data.size != size) {
        show_info("Size Mismatch!");
        return 0;
    }

    size = data.size; // 虽然理论上应该匹配，但保持赋值以防万一
    memcpy(maze, data.maze, sizeof(maze));
    memcpy(visited, data.visited, sizeof(visited));
    memcpy(path, data.path, sizeof(path));
    memcpy(user_path, data.user_path, sizeof(user_path));
    cursor_r = data.cursor_r;
    cursor_c = data.cursor_c;
    timer_running = data.timer_running;
    user_solving = data.user_solving;
    race_mode = data.race_mode;
    timed_out = data.timed_out;
    race_user_done = data.race_user_done;
    race_algo_done = data.race_algo_done;
    start_time = time(NULL) - (time_t)data.elapsed;

    draw_maze();
    show_info("Game Loaded");
    return 1;
}

// ---------- 赛跑模式 ----------
void race_mode_run() {
    if (!maze_solvable()) { show_info("No solution!"); return; }
    set_stage(3);
    race_mode = 1;
    race_user_done = 0;
    race_algo_done = 0;
    user_solving = 0;
    reset_paths();
    cursor_r = 1; cursor_c = 1;
    user_path[1][1] = 1;

    Pos stack[MAX_SIZE*MAX_SIZE];
    int top = -1;
    stack[++top] = (Pos){1,1};
    visited[1][1] = 1;
    ai_r = 1; ai_c = 1;

    int algo_winner = 0, user_winner = 0;
    start_time = time(NULL);
    timer_running = 1;
    show_info("RACE START!");
    draw_maze();
    nodelay(main_win, TRUE);
    int ch;
    while (1) {
        if (!algo_winner && top >= 0) {
            Pos cur = stack[top];
            int r = cur.row, c = cur.col;
            ai_r = r; ai_c = c;
            if (r == size-2 && c == size-2) {
                algo_winner = 1; race_algo_done = 1;
                for (int i = 0; i <= top; i++) path[stack[i].row][stack[i].col] = 1;
            } else if (elapsed_seconds() >= TIMEOUT_SEC) {
                show_info("Race Timeout! AI Wins.");
                algo_winner = 1;
            } else {
                int found = 0;
                for (int d = 0; d < 4; d++) {
                    int nr = r + dr[d], nc = c + dc[d];
                    if (nr>=0&&nr<size&&nc>=0&&nc<size && maze[nr][nc]==0 && visited[nr][nc]==0) {
                        visited[nr][nc] = 1; stack[++top] = (Pos){nr,nc}; found = 1; break;
                    }
                }
                if (!found) { visited[r][c] = 2; top--; }
            }
        } else if (!algo_winner && top < 0) {
            algo_winner = -1;
        }

        ch = wgetch(main_win);
        if (ch == 'q') break;

        if (!user_winner) {
            int nr = cursor_r, nc = cursor_c;
            switch (ch) {
                case KEY_UP: nr--; break;
                case KEY_DOWN: nr++; break;
                case KEY_LEFT: nc--; break;
                case KEY_RIGHT: nc++; break;
            }
            if (nr>=1&&nr<=size-2&&nc>=1&&nc<=size-2&&maze[nr][nc]==0) {
                cursor_r = nr; cursor_c = nc;
                user_path[nr][nc] = 1;
                if (nr==size-2 && nc==size-2) { user_winner = 1; race_user_done = 1; }
            }
        }

        draw_maze();
        char msg[64];
        if (user_winner && !algo_winner) snprintf(msg, sizeof(msg), "YOU WIN!");
        else if (algo_winner == 1 && !user_winner) snprintf(msg, sizeof(msg), "AI WINS!");
        else if (algo_winner == 1 && user_winner) snprintf(msg, sizeof(msg), "DRAW!");
        else snprintf(msg, sizeof(msg), "RACING...");
        show_info(msg);

        if (algo_winner == 1 || user_winner) { napms(1500); break; }
        napms(50);
    }
    nodelay(main_win, FALSE);
    wtimeout(main_win, 100);
    race_mode = 0;
    timer_running = 0;
    set_stage(2);
}

// ---------- 主程序 ----------
void game_loop() {
    wtimeout(main_win, 100);
    int ch;
    while ((ch = wgetch(main_win)) != 'q') {
        if (ch == ERR) ch = 0;

        if (user_solving) {
            int nr = cursor_r, nc = cursor_c, moved = 0;
            switch (ch) {
                case KEY_UP: nr--; moved=1; break;
                case KEY_DOWN: nr++; moved=1; break;
                case KEY_LEFT: nc--; moved=1; break;
                case KEY_RIGHT: nc++; moved=1; break;
            }
            if (moved) {
                if (nr>=1&&nr<=size-2&&nc>=1&&nc<=size-2&&maze[nr][nc]==0) {
                    cursor_r = nr; cursor_c = nc;
                    user_path[nr][nc] = 1;
                    if (nr==size-2 && nc==size-2) {
                        timer_running = 0; user_solving = 0;
                        show_info("SOLVED!");
                        draw_maze();
                        napms(2000);
                        continue;
                    }
                }
                draw_maze(); show_info("Solving...");
                continue;
            }
            if (ch == 'u') { user_solving = 0; show_info("Quit Manual"); continue; }
            continue;
        }

        switch (ch) {
            case KEY_UP: if (cursor_r > 1) cursor_r--; break;
            case KEY_DOWN: if (cursor_r < size-2) cursor_r++; break;
            case KEY_LEFT: if (cursor_c > 1) cursor_c--; break;
            case KEY_RIGHT: if (cursor_c < size-2) cursor_c++; break;
            case 'z': if ((cursor_r==1&&cursor_c==1) || (cursor_r==size-2&&cursor_c==size-2)) break; maze[cursor_r][cursor_c] = 1; reset_paths(); if (current_stage == 0) set_stage(1); break;
            case 'x': case 'e': if ((cursor_r==1&&cursor_c==1) || (cursor_r==size-2&&cursor_c==size-2)) break; maze[cursor_r][cursor_c] = 0; reset_paths(); break;
            case 's': if (maze_solvable()) { reset_paths(); draw_maze(); solve_maze_internal(1); set_stage(2); } else { show_info("No Path"); napms(500); show_info(" "); napms(500); show_info("No Path"); } break;
            case 'c': if(maze_solvable()) { show_info("Solvable"); napms(500); show_info(" "); napms(500); show_info("Solvable"); if (current_stage == 1) set_stage(2); } else { if (current_stage == 2) set_stage(1); show_info("No Path"); napms(500); show_info(" "); napms(500); show_info("No Path"); } napms(1500); break;
            case 'u': if (maze_solvable()) { user_solve_mode(); set_stage(3); } else { show_info("No Path"); napms(500); show_info(" "); napms(500); show_info("No Path"); } break;
            case 'i': sprinkle_inspiration(); if (current_stage == 0) set_stage(1); break;
            case 'r': init_maze(); reset_paths(); cursor_r=1; cursor_c=1; timer_running=0; timed_out=0; race_mode=0; show_info("Reset"); set_stage(0); napms(1000); break;
            case 'b': save_progress(); napms(1000); break;
            case 'l': load_progress(); napms(1000); break;
            case 'a': race_mode_run(); break;
        }

        check_timeout();
        if (timed_out) {
            reset_paths();
            solve_maze_internal(1);
            show_info("Timed Out");
            timed_out = 0;
            timer_running = 0;
            napms(2000);
        }
        draw_maze();
        show_info(NULL);
    }
}

int main() {
    // 1. 初始化环境，显示菜单，确定 size 和 load_on_start
    init_game();

    // 2. 根据确定的 size 创建窗口
    setup_windows();

    // 3. 根据选择加载或初始化数据
    if (load_on_start) {
        // 尝试加载，如果失败（例如文件在校验时被删除），则创建新迷宫
        if (!load_progress()) {
            init_maze();
            reset_paths();
            cursor_r = 1; cursor_c = 1;
            draw_maze();
            show_info("Load failed. New maze created.");
        }
        // load_progress 内部已经调用了 draw_maze
    } else {
        // 新游戏
        init_maze();
        reset_paths();
        cursor_r = 1; cursor_c = 1;
        timer_running = 0;
        draw_maze();
        show_info("Welcome");
    }

    // 4. 运行主循环
    game_loop();

    // 5. 清理
    cleanup();
    return 0;
}

