#include <iostream>

struct Packet {
	void* data = nullptr;
};

void work(Packet& pkt) {
	int* i = new int(10);
	pkt.data = i;
}

int main() {
	int i = 1000;
	while (i > 0) {
		Packet pkt;
		work(pkt);
		if (!pkt.data) {
			std::cout << "pkt data is nullptr" << std::endl;
		}
		//else std::cout << *((int*)pkt.data) << std::endl;
		i--;
	}

	return 0;
}