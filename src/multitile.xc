#include <platform.h>

typedef chanend chanend_t;

extern "C" {

void main_tile0(chanend_t, unsigned);
void main_tile1(chanend_t, unsigned);

}

int main(void)
{
    chan c_tile0_tile1;

    par {
        on tile[0]: main_tile0(c_tile0_tile1, get_tile_id(tile[1]));
        on tile[1]: main_tile1(c_tile0_tile1, get_tile_id(tile[0]));
    }
    return 0;
}