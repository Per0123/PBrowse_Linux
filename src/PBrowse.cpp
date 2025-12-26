#include <SDL2/SDL.h>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <filesystem>
#include "font8x8_basic.h"

struct DisplayItem {
    std::string text;
    std::string link; // empty if not a link
};

struct Tab {
    std::string url;
    std::string title;
    std::vector<DisplayItem> items;
};

// ---- SDL2 text drawing ----
void draw_char(SDL_Renderer* ren, int x, int y, char c, SDL_Color color){
    if(c<0 || c>127) return;
    SDL_SetRenderDrawColor(ren,color.r,color.g,color.b,color.a);
    for(int row=0; row<8; row++)
        for(int col=0; col<8; col++)
            if(font8x8_basic[(int)c][row] & (1<<col))
                SDL_RenderDrawPoint(ren,x+col,y+row);
}

void draw_text(SDL_Renderer* ren, int x,int y,const std::string &text, SDL_Color color){
    int cx=x;
    for(char c:text){
        draw_char(ren,cx,y,c,color);
        cx+=8;
    }
}

// ---- libcurl HTTP fetch ----
size_t curl_write(void* ptr, size_t size, size_t nmemb, std::string* data){
    data->append((char*)ptr, size*nmemb);
    return size*nmemb;
}

std::string load_url(const std::string &url){
    CURL* curl = curl_easy_init();
    std::string content;
    if(curl){
        curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,curl_write);
        curl_easy_setopt(curl,CURLOPT_WRITEDATA,&content);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl,CURLOPT_USERAGENT,"MiniPBrowse/1.0");
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            content="Error fetching page: "+std::string(curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }else{
        content="Error: libcurl init failed";
    }
    return content;
}

// ---- Minimal HTML parser for text + links ----
std::vector<DisplayItem> parse_html(const std::string &html){
    std::vector<DisplayItem> items;
    bool in_tag=false;
    bool in_link=false;
    std::string text, href;

    for(size_t i=0;i<html.size();i++){
        char c=html[i];
        if(c=='<'){
            in_tag=true;
            if(!text.empty()){
                items.push_back({text,in_link?href:""});
                text.clear();
            }
            size_t j=i+1; bool closing=false;
            if(j<html.size() && html[j]=='/'){ closing=true; j++; }
            std::string tagname;
            while(j<html.size() && html[j]!='>' && !isspace(html[j])) tagname+=tolower(html[j++]);
            if(tagname=="a" && !closing){
                in_link=true;
                size_t href_pos=html.find("href=",i);
                if(href_pos!=std::string::npos){
                    size_t start=html.find('"',href_pos);
                    size_t end=html.find('"',start+1);
                    if(start!=std::string::npos && end!=std::string::npos)
                        href=html.substr(start+1,end-start-1);
                }
            }
            if(closing && tagname=="a") in_link=false;
        }else if(c=='>'){ in_tag=false; }
        else if(!in_tag) text+=c;
    }
    if(!text.empty()) items.push_back({text,in_link?href:""});
    return items;
}

// ---- Main ----
int main(int argc,char** argv){
    SDL_Init(SDL_INIT_VIDEO);
    CURLcode curl_res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if(curl_res != CURLE_OK){ std::cerr<<"libcurl init failed\n"; return 1; }

    SDL_Window* win=SDL_CreateWindow("PBrowse",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1000,600,0);
    SDL_Renderer* ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);

    int tab_height=18, url_height=16, search_height=16;
    std::vector<Tab> tabs;
    int current_tab=0;
    int scroll_offset=0;

    // ---- Initial tab: load local HTML file ----
    std::filesystem::path path = std::filesystem::absolute("src/StartTab.html");
    Tab t;
    t.url = "file://" + path.string();
    t.title = "Start Page";
    t.items = parse_html(load_url(t.url));
    tabs.push_back(t);

    bool running=true;
    SDL_Event e;
    SDL_StartTextInput();
    std::string url_text="", search_text="";
    bool url_active=true, search_active=false;

    while(running){
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) running=false;
            else if(e.type==SDL_TEXTINPUT){
                if(url_active) url_text+=e.text.text;
                else if(search_active) search_text+=e.text.text;
            }else if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_BACKSPACE){
                    if(url_active && !url_text.empty()) url_text.pop_back();
                    if(search_active && !search_text.empty()) search_text.pop_back();
                }else if(e.key.keysym.sym==SDLK_TAB){
                    url_active = !url_active;
                    search_active = !search_active;
                }else if(e.key.keysym.sym==SDLK_RETURN){
                    if(url_active && !url_text.empty()){
                        std::string full_url=url_text;
                        if(full_url.find("http")!=0 && full_url.find("file://")!=0)
                            full_url="http://"+full_url;
                        tabs[current_tab].url=full_url;
                        tabs[current_tab].title=full_url;
                        tabs[current_tab].items=parse_html(load_url(full_url));
                        url_text="";
                        scroll_offset=0;
                    }else if(search_active && !search_text.empty()){
                        std::string search_url="https://www.google.com/search?q="+search_text;
                        tabs[current_tab].url=search_url;
                        tabs[current_tab].title="Search";
                        tabs[current_tab].items=parse_html(load_url(search_url));
                        search_text="";
                        scroll_offset=0;
                    }
                }else if(e.key.keysym.sym==SDLK_DOWN) scroll_offset+=10;
                else if(e.key.keysym.sym==SDLK_UP) scroll_offset-=10;
                if(scroll_offset<0) scroll_offset=0;
            }else if(e.type==SDL_MOUSEWHEEL){
                scroll_offset -= e.wheel.y * 10;
                if(scroll_offset<0) scroll_offset=0;
            }else if(e.type==SDL_MOUSEBUTTONDOWN){
                int mx=e.button.x, my=e.button.y;
                int tx=0;
                for(int i=0;i<tabs.size();i++){
                    SDL_Rect r={tx,0,150,tab_height};
                    SDL_Rect close={tx+140,0,10,tab_height};
                    if(mx>=r.x && mx<r.x+r.w && my>=r.y && my<r.y+r.h) current_tab=i;
                    if(mx>=close.x && mx<close.x+close.w && my>=close.y && my<close.y+close.h){
                        if(tabs.size() > 1) {
                            tabs.erase(tabs.begin()+i);
                            if(current_tab == i) {
                                // If we closed the current tab, move to previous if possible
                                current_tab = (i == 0) ? 0 : i - 1;
                            } else if(current_tab > i) {
                                // Adjust current_tab index if after the closed tab
                                current_tab--;
                            }
                        }
                    }
                    tx+=150;
                }

                SDL_Rect plus={tx,0,18,tab_height};
                if(mx>=plus.x && mx<plus.x+plus.w && my>=plus.y && my<plus.y+plus.h) {
                    std::filesystem::path path = std::filesystem::absolute("src/StartTab.html");
                    Tab newtab;
                    newtab.url = "file://" + path.string();
                    newtab.title = "Start Page";
                    newtab.items = parse_html(load_url(newtab.url));
                    tabs.push_back(newtab);
                    current_tab = tabs.size()-1;
                }

                // Click links
                SDL_Rect content_area = {0, tab_height + url_height + search_height + 4, 1000, 600 - (tab_height + url_height + search_height + 4)};
                int y = 0 - scroll_offset;
                for(auto &it: tabs[current_tab].items){
                    if(!it.link.empty()){
                        int lx=10, ly=content_area.y + y, lw=it.text.size()*8, lh=8;
                        if(mx>=lx && mx<lx+lw && my>=ly && my<ly+lh){
                            std::string link_url=it.link;
                            if(link_url.find("http")!=0 && link_url.find("file://")!=0)
                                link_url="http://"+link_url;
                            tabs[current_tab].url=link_url;
                            tabs[current_tab].title=link_url;
                            tabs[current_tab].items=parse_html(load_url(link_url));
                            scroll_offset=0;
                        }
                    }
                    y+=10;
                }
            }
        }

        // ---- Render ----
        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);

        // Tabs
        int tx=0;
        for(int i=0;i<tabs.size();i++){
            SDL_Rect r={tx,0,150,tab_height};
            SDL_SetRenderDrawColor(ren,i==current_tab?200:80,80,80,255);
            SDL_RenderFillRect(ren,&r);
            SDL_SetRenderDrawColor(ren,255,255,255,255);
            SDL_RenderDrawRect(ren,&r);
            draw_text(ren, tx+2, 0, tabs[i].title.substr(0,15), {255,255,255,255});
            draw_text(ren, tx+142, 0, "X", {255,0,0,255});
            tx+=150;
        }
        SDL_Rect plus={tx,0,18,tab_height};
        SDL_SetRenderDrawColor(ren,0,200,0,255);
        SDL_RenderFillRect(ren,&plus);
        draw_text(ren, tx+2,0,"+",{255,255,255,255});

        // URL bar
        SDL_Rect url_rect={0,tab_height,1000,url_height};
        SDL_SetRenderDrawColor(ren,40,40,40,255);
        SDL_RenderFillRect(ren,&url_rect);
        draw_text(ren,2,tab_height,url_text.empty()?tabs[current_tab].url:url_text,{255,255,255,255});
        int cursor_x=2+(url_text.empty()?tabs[current_tab].url.size():url_text.size())*8;
        SDL_SetRenderDrawColor(ren,255,255,255,255);
        SDL_RenderDrawLine(ren,cursor_x,tab_height,cursor_x,tab_height+8);

        // Search bar
        SDL_Rect search_rect={0,tab_height+url_height,1000,search_height};
        SDL_SetRenderDrawColor(ren,40,40,40,255);
        SDL_RenderFillRect(ren,&search_rect);
        draw_text(ren,2,tab_height+url_height, search_text.empty()?"Search...":search_text,{200,200,200,255});
        int cursor_sx=2+search_text.size()*8;
        SDL_RenderDrawLine(ren,cursor_sx,tab_height+url_height,cursor_sx,tab_height+url_height+8);

        // Page content with clipping
        SDL_Rect content_area = {0, tab_height + url_height + search_height + 4, 1000, 600 - (tab_height + url_height + search_height + 4)};
        SDL_RenderSetClipRect(ren, &content_area);

        int y=0 - scroll_offset;
        for(auto &it: tabs[current_tab].items){
            SDL_Color col = it.link.empty()?SDL_Color{220,220,220,255}:SDL_Color{0,128,255,255};
            draw_text(ren,10,content_area.y + y,it.text,col);
            if(!it.link.empty())
                SDL_RenderDrawLine(ren,10,content_area.y + y + 8,10+it.text.size()*8,content_area.y + y + 8);
            y += 10;
        }

        SDL_RenderSetClipRect(ren,NULL); // reset clipping for UI
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_StopTextInput();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    curl_global_cleanup();
    SDL_Quit();
    return 0;
}

/*
g++ -O2 -std=c++17 src/PBrowse.cpp -I. `pkg-config --cflags --libs sdl2` -lcurl -o PBrowse
*/
