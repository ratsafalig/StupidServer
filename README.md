# StupidServer

A stupid multi-thread iocp based server

Run tcp and http protocol

For mapping url into file system,simply write code like this:
'''
int main(){

	Tcp * tcp = new Tcp();
	Http * http = new Http();

	//binding http service to iocp
	tcp->http = http;

	tcp->init();
	http->init();

	http->map((char *)"/a", 2, (char*)"hello_world.html", 17);

	tcp->start();
	http->start();

	system("pause");
	return 0;
};
'''
