class Consoles
{
  public:
    Consoles();
    ~Consoles() {
        // stop();
    };
	int getCurrentConsole();
	int setCurrentConsole(int);
	int currentConsoleIndex;
  private:
	int sp;
};