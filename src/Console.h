#include <iostream>
using namespace std;

class Console
{
  public:
    Console(int _led_position, int _led_width, int _selector_position, string _name);
	int led_position;
	int led_width;
	int selector_position;
	string name;
};