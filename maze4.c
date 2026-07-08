// maze4_modified.c
// 升级版迷宫：侧边栏布局 + AI光标(压迫感) + 右置信息
// 修改说明：
// 1. 移除了所有的WASD移动控制逻辑，仅保留方向键操作。
// 2. 将main()函数模块化，拆分为init_game(), setup_windows(), game_loop(), cleanup()。
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
#include <stdarg.h> // 用于格式化输出

#define MAX_SIZE 100
#define SAVE_FILE "maze_save.bin"
#define TIMEOUT_SEC 600

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

typedef struct {
    int row, col;
} Pos;

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
int ai_r = -1, ai_c = -1; // AI 光标位置

time_t start_time;
time_t race_start_time;
int timer_running = 0;
int race_mode = 0;
int race_user_done = 0;
int race_algo_done = 0;
int user_solving = 0;
int timed_out = 0;
int use_color = 0;

// 颜色定义
enum { C_WALL = 1, C_PATH, C_START, C_END, C_ALGO, C_USER, C_CURSOR, C_BG, C_DEAD, C_AI };

// ---------- 辅助函数：右对齐打印 ----------
void print_right(WINDOW *w, int y, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    int max_x, max_y;
    getmaxyx(w, max_y, max_x);
    // 防止溢出
    int x = max_x - strlen(buf) - 1;
    if (x < 0) x = 0;
    mvwprintw(w, y, x, "%s", buf);
}

// ---------- 初始化与设置 ----------
void init_colors_if_possible() {
    if (has_colors()) {
        start_color();
        use_color = 1;
        init_pair(C_WALL, COLOR_BLACK, COLOR_WHITE); // 砖墙
        init_pair(C_PATH, COLOR_BLACK, COLOR_BLUE); // 蓝路
        init_pair(C_START, COLOR_BLACK, COLOR_GREEN);
        init_pair(C_END, COLOR_BLACK, COLOR_RED);
        init_pair(C_ALGO, COLOR_BLACK, COLOR_CYAN); // AI足迹
        init_pair(C_USER, COLOR_BLACK, COLOR_MAGENTA);
        init_pair(C_CURSOR, COLOR_WHITE, COLOR_BLACK);
        init_pair(C_BG, COLOR_WHITE, COLOR_BLACK);
        init_pair(C_DEAD, COLOR_BLACK, COLOR_RED);
        // AI光标：红字黄底，极具压迫感
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
    ai_r = -1;
    ai_c = -1; // 隐藏AI光标
}

double elapsed_seconds() {
    if (!timer_running) return 0;
    return difftime(time(NULL), start_time);
}

// 模块化：初始化环境和获取输入
void init_game() {
    srand((unsigned)time(NULL));
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colors_if_possible();

    printw("Maze Size (3-50): ");
    refresh();
    int internal;
    echo();
    scanw("%d", &internal);
    noecho();
    if (internal < 3) internal = 3;
    if (internal > 50) internal = 50;
    size = internal + 2;
    if (size > MAX_SIZE) size = MAX_SIZE;
}

// 模块化：创建窗口
void setup_windows() {
    int maze_h = size + 2;
    int maze_w = size + 2;
    int info_w = 32; // 侧边栏宽度
    int total_w = maze_w + info_w + 2;

    // 居中计算
    int start_y = 2;
    int start_x = (COLS - total_w) / 2;
    if (start_x < 1) start_x = 1;

    main_win = newwin(maze_h, maze_w, start_y, start_x);
    info_win = newwin(maze_h, info_w, start_y, start_x + maze_w + 1);
    keypad(main_win, TRUE);
}

// 模块化：清理资源
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
            int y = i+1, x = j+1;
            chtype ch = ' ';
            int color = C_PATH;
            int is_special = 0;

            // 优先级判断
            int is_start = (i == 1 && j == 1);
            int is_end = (i == size-2 && j == size-2);
            int is_user = (i == cursor_r && j == cursor_c);
            int is_ai = (i == ai_r && j == ai_c && ai_r != -1);

            if (use_color) {
                if (is_ai) {
                    color = C_AI;
                    ch = 'O'; // AI 躯体
                    is_special = 1;
                } else if (is_user) {
                    color = C_CURSOR;
                    ch = 'X';
                    is_special = 1;
                } else if (user_path[i][j]) {
                    color = C_USER;
                    ch = ACS_DIAMOND;
                    is_special = 1;
                } else if (path[i][j]) {
                    color = C_ALGO;
                    ch = ACS_ULCORNER;
                    is_special = 1;
                } else if (is_start) {
                    color = C_START;
                    ch = 'S';
                    is_special = 1;
                } else if (is_end) {
                    color = C_END;
                    ch = 'E';
                    is_special = 1;
                } else if (visited[i][j] == 2) {
                    color = C_DEAD;
                    ch = ACS_BULLET;
                    is_special = 1;
                } else if (maze[i][j] == 1) {
                    color = C_WALL;
                    ch = ACS_CKBOARD;
                    is_special = 1;
                } else {
                    color = C_PATH;
                    ch = ACS_HLINE;
                }
                wbkgdset(main_win, COLOR_PAIR(color));
                mvwaddch(main_win, y, x, ch);
                wbkgdset(main_win, COLOR_PAIR(C_BG));
            } else {
                if (is_ai) ch = 'O';
                else if (is_user) ch = 'X';
                else if (user_path[i][j]) ch = '@';
                else if (path[i][j]) ch = '*';
                else if (is_start) ch = 'S';
                else if (is_end) ch = 'E';
                else if (maze[i][j] == 1) ch = '#';
                else ch = '.';
                mvwaddch(main_win, y, x, ch);
            }
        }
    }
    wrefresh(main_win);
}

// ---------- 信息栏 (右侧) ----------
void show_info(const char *msg) {
    werase(info_win);
    box(info_win, 0, 0);
    double el = elapsed_seconds();
    int mm = (int)el / 60;
    int ss = (int)el % 60;

    // 状态构建
    char mode_str[32] = "EDIT";
    if (user_solving) strcpy(mode_str, "USER SOLVE");
    else if (race_mode) strcpy(mode_str, "RACE MODE");
    else if (timed_out) strcpy(mode_str, "TIMEOUT");

    // 第1行：时间 (右对齐)
    print_right(info_win, 1, "Time: %02d:%02d", mm, ss);
    // 第2行：模式 (右对齐)
    print_right(info_win, 2, "Mode: %s", mode_str);
    // 第3行：坐标 (右对齐)
    print_right(info_win, 3, "Cursor: %d,%d", cursor_r, cursor_c);

    // 分隔线
    mvwhline(info_win, 4, 1, ACS_HLINE, getmaxx(info_win)-2);

    // 第5行：主要消息
    if (msg && *msg) {
        print_right(info_win, 5, "%s", msg);
    } else {
        print_right(info_win, 5, "Ready...");
    }

    // 底部提示
    mvwprintw(info_win, getmaxy(info_win)-2, 1, "z/x:Edit u:Manual s:AI");
    print_right(info_win, getmaxy(info_win)-2, "a:Race b:Save l:Load");
    mvwprintw(info_win, getmaxy(info_win)-1, 1, "i:Hint c:Check r:Reset");
    print_right(info_win, getmaxy(info_win)-1, "q:Quit");

    wrefresh(info_win);
}

void check_timeout() {
    if (!timer_running || timed_out) return;
    if (elapsed_seconds() >= TIMEOUT_SEC) {
        timed_out = 1;
        show_info("Timeout! AI taking over...");
    }
}

// ---------- 核心算法 (带AI光标更新) ----------
int solve_maze_internal(int animate) {
    reset_paths();
    Pos stack[MAX_SIZE*MAX_SIZE];
    int top = -1;
    stack[++top] = (Pos){1, 1};
    visited[1][1] = 1;
    ai_r = 1; ai_c = 1; // 初始AI位置

    while (top >= 0) {
        Pos cur = stack[top];
        int r = cur.row, c = cur.col;
        ai_r = r; ai_c = c; // 更新AI光标

        if (r == size-2 && c == size-2) {
            for (int i = 0; i <= top; i++) path[stack[i].row][stack[i].col] = 1;
            if (animate) {
                draw_maze();
                show_info("Solved!");
            }
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
                if (animate) {
                    draw_maze();
                    show_info("AI Searching...");
                    napms(30);
                }
                break;
            }
        }

        if (!found) {
            visited[r][c] = 2;
            top--;
            if (animate) {
                draw_maze();
                show_info("AI Backtracking...");
                napms(20);
            }
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
    return 1;
}

void sprinkle_inspiration() {
    echo();
    mvwprintw(info_win, 5, 1, "Density (0-100): ");
    wrefresh(info_win);
    char buf[16] = {0};
    int n = 0, ch;
    while ((ch = wgetch(info_win)) != '\n' && n < 15) {
        if (ch >= '0' && ch <= '9') buf[n++] = ch;
    }
    buf[n] = 0;
    noecho();
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
    if (written != sizeof(data)) {
        unlink(SAVE_FILE);
        show_info("Write Error!");
    } else {
        show_info("Saved Successfully");
    }
}

int load_progress() {
    int fd = open(SAVE_FILE, O_RDONLY);
    if (fd < 0) { show_info("No Save File"); return 0; }
    SaveData data;
    ssize_t n = read(fd, &data, sizeof(data));
    close(fd);
    if (n != sizeof(data) || data.size < 5 || data.size > MAX_SIZE) {
        show_info("Corrupt Save"); return 0;
    }
    size = data.size;
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

// ---------- 赛跑模式 (带AI光标，已移除WASD) ----------
void race_mode_run() {
    if (!maze_solvable()) {
        show_info("No solution!");
        return;
    }
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
    ai_r = 1; ai_c = 1; // AI 初始位置

    int algo_winner = 0, user_winner = 0;
    start_time = time(NULL);
    timer_running = 1;
    show_info("RACE START!");
    draw_maze();
    nodelay(main_win, TRUE);

    int ch;
    while (1) {
        // AI Step
        if (!algo_winner && top >= 0) {
            Pos cur = stack[top];
            int r = cur.row, c = cur.col;
            ai_r = r; ai_c = c; // 实时更新AI光标

            if (r == size-2 && c == size-2) {
                algo_winner = 1;
                race_algo_done = 1;
                for (int i = 0; i <= top; i++) path[stack[i].row][stack[i].col] = 1;
            } else {
                int found = 0;
                for (int d = 0; d < 4; d++) {
                    int nr = r + dr[d], nc = c + dc[d];
                    if (nr>=0&&nr<size&&nc>=0&&nc<size && maze[nr][nc]==0 && visited[nr][nc]==0) {
                        visited[nr][nc] = 1;
                        stack[++top] = (Pos){nr,nc};
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    visited[r][c] = 2;
                    top--;
                }
            }
        } else if (!algo_winner && top < 0) algo_winner = -1;

        ch = wgetch(main_win);
        if (ch == 'q') break;

        // User Step
        if (!user_winner) {
            int nr = cursor_r, nc = cursor_c;
            // 仅保留方向键控制
            switch (ch) {
                case KEY_UP:    nr--; break;
                case KEY_DOWN:  nr++; break;
                case KEY_LEFT:  nc--; break;
                case KEY_RIGHT: nc++; break;
            }
            if (nr>=1&&nr<=size-2&&nc>=1&&nc<=size-2&&maze[nr][nc]==0) {
                cursor_r = nr; cursor_c = nc;
                user_path[nr][nc] = 1;
                if (nr==size-2 && nc==size-2) {
                    user_winner = 1;
                    race_user_done = 1;
                }
            }
        }

        draw_maze();
        char msg[64];
        if (user_winner && !algo_winner) snprintf(msg, sizeof(msg), "YOU WIN!");
        else if (algo_winner == 1 && !user_winner) snprintf(msg, sizeof(msg), "AI WINS!");
        else if (algo_winner == 1 && user_winner) snprintf(msg, sizeof(msg), "DRAW!");
        else snprintf(msg, sizeof(msg), "RACING...");
        show_info(msg);

        if (algo_winner == 1 || user_winner) {
            napms(1500);
            break;
        }
        napms(50);
    }

    nodelay(main_win, FALSE);
    race_mode = 0;
    timer_running = 0;
}

// ---------- 主程序 (侧边栏布局 + 模块化) ----------

// 模块化：主游戏循环
void game_loop() {
    int ch;
    while ((ch = wgetch(main_win)) != 'q') {
        if (user_solving) {
            int nr = cursor_r, nc = cursor_c, moved = 0;
            switch (ch) {
                case KEY_UP:    nr--; moved=1; break;
                case KEY_DOWN:  nr++; moved=1; break;
                case KEY_LEFT:  nc--; moved=1; break;
                case KEY_RIGHT: nc++; moved=1; break;
            }
            if (moved) {
                if (nr>=1&&nr<=size-2&&nc>=1&&nc<=size-2&&maze[nr][nc]==0) {
                    cursor_r = nr; cursor_c = nc;
                    user_path[nr][nc] = 1;
                    if (nr==size-2 && nc==size-2) {
                        timer_running = 0;
                        user_solving = 0;
                        show_info("SOLVED!");
                        draw_maze();
                        napms(2000);
                        continue;
                    }
                }
                draw_maze();
                show_info("Solving...");
                continue;
            }
            if (ch == 'u') {
                user_solving = 0;
                show_info("Quit Manual");
                continue;
            }
            continue;
        }

        // 编辑模式逻辑
        switch (ch) {
            case KEY_UP:    if (cursor_r > 1) cursor_r--; break;
            case KEY_DOWN:  if (cursor_r < size-2) cursor_r++; break;
            case KEY_LEFT:  if (cursor_c > 1) cursor_c--; break;
            case KEY_RIGHT: if (cursor_c < size-2) cursor_c++; break;
            case 'z':
                if ((cursor_r==1&&cursor_c==1) || (cursor_r==size-2&&cursor_c==size-2)) break;
                maze[cursor_r][cursor_c] = 1;
                reset_paths();
                break;
            case 'x':
            case 'e':
                if ((cursor_r==1&&cursor_c==1) || (cursor_r==size-2&&cursor_c==size-2)) break;
                maze[cursor_r][cursor_c] = 0;
                reset_paths();
                break;
            case 's':
                reset_paths();
                draw_maze();
                solve_maze_internal(1);
                break;
            case 'c':
                show_info(maze_solvable() ? "Solvable" : "No Path");
                napms(1500);
                break;
            case 'u':
                if (maze_solvable()) user_solve_mode();
                else show_info("No Path");
                break;
            case 'i':
                sprinkle_inspiration();
                break;
            case 'r':
                init_maze();
                reset_paths();
                cursor_r=1; cursor_c=1;
                timer_running=0; timed_out=0; race_mode=0;
                show_info("Reset");
                napms(1000);
                break;
            case 'b':
                save_progress();
                napms(1000);
                break;
            case 'l':
                if (load_progress()) napms(1000);
                break;
            case 'a':
                race_mode_run();
                break;
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
    // 1. 初始化环境和参数
    init_game();
    
    // 2. 设置窗口布局
    setup_windows();
    
    // 3. 游戏数据初始化
    init_maze();
    reset_paths();
    cursor_r = 1; 
    cursor_c = 1;
    timer_running = 0;
    
    // 4. 初始绘制
    draw_maze();
    show_info("Welcome");
    
    // 5. 运行主循环
    game_loop();
    
    // 6. 清理并退出
    cleanup();
    
    return 0;
}
