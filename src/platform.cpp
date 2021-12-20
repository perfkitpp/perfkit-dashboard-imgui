
#if _WIN32
#    include <Windows.h>

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   PSTR lpCmdLine, INT nCmdShow)
{
    int main(int, char**);
    return main(1, nullptr);
}

void InitPlatform()
{
}

#endif
