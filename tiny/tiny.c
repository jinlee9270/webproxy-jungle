/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */

// 1. 입력 : ./tiny 8000
// 2. aws : 15.164.165.184 8000 접속

#include "csapp.h"

void doit(int fd);
void echo(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) { // argc : 옵션의 개수(arg count), argv : 옵션 문자열의 배열(내가 입력하는 스트링 : ./tiny 8000) 
  // 인자 갯수 모를때.. (int argc, char **argv)세트로 씀 // argv가 스트링의 배열이고 *argv일때 0번째 스트링을 가리킴, 스트링이 char *
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE]; // 크기를 모르니까 맥스로 받음
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) { // (1)/.tiny port(2) 2개, 2개가 아닐 경우 입력오류 띄움
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // 내가 입력한 str address // argv[0] : ./tiny
    exit(1); // ./tiny만 입력하면 argc = 1개라서 에러 => 에러메시지 : ./tiny <port>
  }

  /* Open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다. 인자로 포트번호를 넘겨준다. */
  // Open_listenfd는 요청받을 준비가된 듣기 식별자를 리턴한다 = listenfd // fd : 012 기본 들어있고, 3부터 들어옴
  listenfd = Open_listenfd(argv[1]); /* 듣기 소켓 오픈 */ // argv[1] : 8000 포트를 넣음 arg의 1번 엘레멘트가 포트인 것/ getaddinfo, socket, bind, listen의 역할들 한번에 수행
  while (1) { /* 무한 서버 루프 실행 */
    clientlen = sizeof(clientaddr); // accpet 함수 인자에 넣기위한 주소길이를 계산 (addr받은건 없지만 일단 사이즈 계산..)

    /* 반복적으로 연결 요청을 접수 */
    // accept 함수는 1. 듣기 식별자, 2. 소켓주소구조체의 주소, 3. 주소(소켓구조체)의 길이를 인자로 받는다.
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept /* 반복적으로 연결 요청 접수*/
            // Accept하면서 동시에 clientaddr 등을 받아옴
    // Getaddrinfo는 호스트(서버) 이름: 호스트 주소, 서비스 이름: 포트 번호 의 스트링 표시를 소켓 주소 구조체로 변환 (모든 프로토콜에 대해)
    // Getnameinfo는 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
                    // clentaddr, clentlen을 받아서 hostname~ 으로 변환
    printf("Accepted connection from (%s, %s)\n", hostname, port); // hostname,   port

    doit(connfd);  // line:netp:tiny:close 
    Close(connfd); /* 자신 쪽의 연결 끝(소켓)을 닫음 */
  }
}

/* 숙제 11.6-A */
void echo(int connfd) {
  size_t n;
  char buf[MAXLINE];
  rio_t rio; // 저자가 만든 패키지. 시스템 수준의 입출력 리눅스에서 쓰는 unix I/O와 비슷.
  // 리오 패키지를 쓰는 이유 : Rio 패키지는 짧은 카운트(short count)를 자동으로 처리한다 (Unix I/O 와 차이) .
  // short count가 발생할 수 있는 네트워크 프로그램 같은 응용에서 편리하고 안정적이고 효율적인 I/O 패키지이다
  /* 보통 short counts는 다음과 같은 상황일 때 발생한다.
    EOF (end-of-file)을 읽는 도중 만난 경우
    text lines 을 터미널로부터 읽어올 때 (예측이 힘듦)
    네트워크 소켓 통신 시 */
  Rio_readinitb(&rio, connfd); // connfd와 &rio를 연결한다. 읽기 버퍼의 포맷을 초기화하는 함수 rio_readinitb 함수는 한 개의 빈 버퍼를 설정하고, 이 버퍼와 한 개의 오픈한 파일 식별자를 연결한다.
  while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) { // &rio를 buf에 복사하고 그 사이즈 = n, rio_readn 함수는 식별자 fd의 현재 파일 위치에서 메모리 위치 usrbuf로 최대 n바이트를 전송한다.
    printf("server received %d bytes\n", (int)n);
    Rio_writen(connfd, buf, n); // buf를 connfd에 쓴다 (클라에서 줌)
    // readline을 하면 rio안의 내용이 사라져서 buf에 넣어놓고 connfd출력하는데에 쓰고 다시 원래대로 돌려줌(writen), rio_writen 함수는 usrbuf에서 식별자 fd로 n바이트로 전송한다.
  }
}

void doit(int fd) { /* 한 개의 HTTP 트랜잭션을 처리 */
  int is_static; 
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio; // rio_readlineb를 위해 rio_t 타입(구조체)의 읽기 버퍼를 선언

  /* Read request line and headers */  
  /* Rio = Robust I/O */
  // rio_t 구조체를 초기화 해준다.
  Rio_readinitb(&rio, fd); // 요청 라인 읽어들임(컴터시스템 p.921-4line) // rio버퍼와 fd. 서버의 connfd를 연결시킨다
  Rio_readlineb(&rio, buf, MAXLINE); // 요청 라인 읽고 분석, rio에 있는 string을 버퍼로 다 옮긴다
  printf("Request headers:\n");
  printf("%s", buf); // 우리가 요청한 자료를 표준 출력 해준다 (godzilla)
  sscanf(buf, "%s %s %s", method, uri, version);
  printf("Get image file uri : %s\n", uri); // 추가코드
      // 같은 문자가 아닐 때 조건문   // GET이거나 HEAD도 아닐 때 /* 숙제 11.11 */
  if (strcasecmp(method, "GET") && strcasecmp(method,  "HEAD")) { // 같으면 0반환이라 if문 안들어감 1은 true라 에러실행 // Tiny는 GET메소드만 지원. 만약 다른 메소드(like POST)를 요청하면. strcasecmp : 문자열비교.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method"); // 에러메시지 보내고, main루틴으로 돌아온다
    return; // 그 후 연결 닫고 다음 요청 기다림. 그렇지 않으면 읽어들이고
  }

  read_requesthdrs(&rio); // GET method라면(0) 그건 읽고, 다른 요청 헤더들은 무시 (그냥 프린트한다)

  /* Parse URI from GET request */ // uri 받은거 분석하는데 이게 정적 컨텐츠이면 1이 나옴 -> 밑에 2번째 if문으로 들감
  is_static = parse_uri(uri, filename, cgiargs); // URI를 파일 이름과, 비어있을 수도 있는 CGI인자 스트링 분석하고 // 정적이면 1 // 파일네임 여기서 prase_uri를 통해 만들어냄 (매개변수처럼..담는 그릇)
  if (stat(filename, &sbuf) < 0) { // 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그 설정 // 버퍼에 파일네임을 넘긴다
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file"); 
    // 만일 파일이 이 디스크 상에 있지 않으면, 에러메시지를 즉시 클라이언트에게 보내고 리턴.
    return;
  }

  if (is_static) { /* Serve static content */ // 만일 요청이 정적 컨텐츠를 위한 것이라면
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // 이 파일이 보통 파일이라는 것과 읽기 권한을 갖고 있는지를 검증한다. 맞으면1, 아니면0
      // sbuf.st_mode : sbuf의 내용 중 st_mode의 값(어떤 타입의 파일인지 16바이트로 나옴)
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 만일 맞다면 정적 컨텐츠를 클라이언트에게 제공
  }
  else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // 만일 요청이 동적 컨텐츠에 대한 것이라면 이 파일이 실행 가능한지 검증하고
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program"); 
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // 만일 그렇다면 진행해서 동적 컨텐츠를 클라이언트에게 제공한다
  } 
}

/* HTTP 응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에 보내며, 
 * 브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML 파일도 함께 보낸다. */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];
  // clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");

  /* Build the HTTP response body */ 
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf)); // buf를 fd에 (위에 클라에서 준 걸 썼으니까 다시 파일 식별자로 보내서 돌려줌)
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* 요청 헤더를 읽고 무시한다 */ // doit에서 GET 구분할 때 사용됨
void read_requesthdrs(rio_t *rp) { // 헤더만 뽑아낼 때 rp를 씀
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE); // 헤더만 다 읽고 첫 줄뺌(요청라인 빼고), 그 다음 while 들어가서 다 출력한다.

  /* strcmp 두 문자열을 비교하는 함수 */
  /* 헤더의 마지막 줄은 비어있기에 \r\n만 buf에 담겨있다면 while문을 탈출한다 */ //버퍼 rp의 마지막 끝을 만날 때까지
  while(strcmp(buf, "\r\n")) {

    // rio_readlineb는 \n을 만날 때 멈춘다
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    // 멈춘 지점까지 출력하고 다시 while
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  /* uri에 cgi-bin이 없다면, 즉 정적 컨텐츠를 요청한다면 1을 리턴한다.*/
  // 예시 : GET /godzilla.jpg HTTP/1.1 -> uri에 cgi-bin이 없다
  /* strstr 으로 cgi-bin이 들어있는지 확인하고 아니면0인데 양수값을 리턴하면 dynamic content를 요구하는 것이기에 조건문을 탈출 */
  if(!strstr(uri, "cgi-bin")) { /* Static content */ // 만일 uri에 cgi-bin 이라는 폴더 내용이 없으면 -> 요청이 정적 컨텐츠를 위한 것이라면
    strcpy(cgiargs, ""); // CGI 인자 스트링은 아무것도 없다
    strcpy(filename, "."); // URI를 ./index.html같은 상대 리눅스 경로 이름으로 바꾼다
    strcat(filename, uri); // filename에 uri 이어붙인다
    //결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html

     /* 예시
      uri : /godzilla.jpg
      ->
      cgiargs : 
      filename : ./godzilla.jpg
    */

    if (uri[strlen(uri)-1] == '/') // 만일 URI가 '/'로 끝난다면
      strcat(filename, "home.html"); // 그 뒤에 기본 파일 이름(home.html)을 filename에 추가한다 -> 해당 이름의 정적컨텐츠가 출력된다
    return 1; // 정적컨텐츠 1 리턴

    // 만약 uri뒤에 '/'이 있다면 그 뒤에 home.html을 붙인다.
    // 내가 브라우저에 http://localhost:8000만 입력하면 바로 뒤에 '/'이 생기는데,
    // '/' 뒤에 home.html을 붙여 해당 위치 해당 이름의 정적 컨텐츠가 출력된다.
  }

  // uri 예시: dynamic: /cgi-bin/adder?first=1213&second=1232 
  else { /* Dynamic content */ // 반면, 만일 이 요청이 동적 컨텐츠를 위한 것이라면
    ptr = index(uri, '?'); // 모든 CGI 인자들을 추출하고 //index 함수는 문자열에서 특정 문자의 위치를 반환한다.
    
    // ? 가 존재한다면
    if (ptr) {
        // 인자로 주어진 값들을 cgiargs 변수에 넣는다
        strcpy(cgiargs, ptr+1);
        *ptr = '\0'; // ptr 초기화
    }
    else
      strcpy(cgiargs, ""); // 없으면 안넣는다
    
    strcpy(filename, "."); // 나머지 URI 부분을 상대 리눅스 파일 이름으로 변환한다
    strcat(filename, uri); // 이어붙이는 함수. 파일네임에 uri를 붙인다

        /* 예시
      uri : /cgi-bin/adder?123&123
      ->
      cgiargs : 123&123
      filename : ./cgi-bin/adder
    */

    return 0; // 동적 컨텐츠.
  }
}


/* 지역 파일의 내용을 포함하고 있는 본체를 갖는 HTTP 응답을 보낸다 */
// 클라이언트가 원하는 파일 디렉토리를 받아온다. 응답 라인과 헤더를 작성하고 서버에게 보낸다. serve~
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF], *fbuf;

  /* Send response headers to client */
  get_filetype(filename, filetype); // 파일 이름의 접미어 부분 검사해서 파일 타입 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 클라이언트에 응답 줄과 응답 헤더를 보낸다 // 버퍼에 넣고 출력
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); // 빈 줄 한 개가 헤더를 종료하고 있음
  /* writen = client(텔넷)에서 출력됨*/
  Rio_writen(fd, buf, strlen(buf)); // 요청한 파일의 내용을 연결 식별자 fd로 복사해서 응답 본체를 보낸다 // 버퍼를 옮김
  /* 서버 쪽에 출력 */
  printf("Response headers:\n"); 
  printf("%s", buf);

  if (!strcasecmp(method, "HEAD")) // 같으면 0(false) 들어가고 끝냄(HEAD가 맞으면)
    return; // void 타입이라 바로 리턴해도 됨(끝내라)

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0); // 열려고 하는 파일의 식별자 번호 리턴. filename을 오픈하고 식별자를 얻어온다
                                // 0 : 읽기 전용이기도하고, 새로 파일을 만드는게 아니니 Null처럼 없다는 의미..없어도 됨
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // mmap : 요청한 파일을 가상메모리 영역으로 매핑한다 / 커널 내부 주소 접근 시에 사용
  // // mmap 호출시 위에서 받아온 모든 요청 정보들(srcfd)을 전부 매핑해서 srcp로 받는다(포인터)
  // Close(srcfd); // srcfd 내용을 메모리로 매핑한 후에 더 이상 이 식별자 필요X, 파일을 닫는다. 안 닫으면 메모리 누수 치명적
  // Rio_writen(fd, srcp, filesize); // 실제로 파일을 클라이언트에게 전송. // srcp내용을 fd에 filesize만큼 복사해서 넣는다
  // Munmap(srcp, filesize); // 매핑된 srcp 주소를 반환한다. 치명적인 메모리 누수를 피한다 

  // // mmap-munmap은 malloc-free처럼 세트 / malloc()은, 큰 메모리 블럭 요청이 들어오면, 내부적으로 mmap()을 써서 메모리를 할당합니다. 포함은 X
  // mmap()은 시스템에서 제공하는 저수준 시스템 콜이며, 특별한 조건일 때, (malloc과 유사하게) 메모리를 할당하는 효과를 볼 수 있습니다. 
  // malloc()은 메모리를 할당하는 C library function이며, 내부적으로 mmap(), brk() 등의 시스템 콜을 써서 구현될 수 있습니다.
  // mmap 공간을 잡는 동시에 내용을 넣음 free시 시스템에 즉각 반영

  /* 숙제문제 11.9 */ // 실행시 위에 srcp부터 ~ Munmap 까지 주석처리 할 것 // 빈공간 할당하고 내용 넣음(Read를 해줘야함) free시 시스템에 즉각반영되지 않음
  fbuf = malloc(filesize); //filesize 만큼의 가상 메모리(힙)를 할당한 후(malloc은 아무것도 없는 빈 상태에서 시작) , Rio_readn 으로 할당된 가상 메모리 공간의 시작점인 fbuf를 기준으로 srcfd 파일을 읽어 복사해넣는다.
  Rio_readn(srcfd, fbuf, filesize); // srcfd 내용을 fbuf에 넣는다(버퍼에 채워줌)
  Close(srcfd); // 윗줄 실행 후 필요 없어져서 닫아준다 // 양 쪽 모두 생성한 파일 식별자 번호인 srcfd 를 Close() 해주고
  Rio_writen(fd, fbuf, filesize); // Rio_writen 함수 (시스템 콜) 을 통해 클라이언트에게 전송한다 
  // fbuf를 fd에다가 넣는다(fbuf는 사실 포인터. 걔를 밀면서 writen, 미는 애는 새로 선언한 usrbuf)
  free(fbuf); // Mmap은 Munmap, malloc은 free로 할당된 가상 메모리를 해제해준다.
}

/*
 * get_filetype - Derive(파생) file type from filename
 * strstr 두번째 인자가 첫번째 인자에 들어있는지 확인한다
 */
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  /* 11.7 숙제 문제 - Tiny 가 MPG 비디오 파일을 처리하도록 하기.  */
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "sample/mpg"); // home.html에서 video src 추가 chrome 에서는 mpg 파일이 작동하지 않기 떄문에 safari로 실행해서 확인함
  else
    strcpy(filetype, "text/plain");
}

/* Tiny는 자식 프로세스 fork후, CGI프로그램을 자식의 컨텍스트에서 실행하여 모든 종류의 동적 컨텐츠를 제공함 */
/* CGI : 공용 게이트웨이 인터페이스, 웹 서버 상에서 사용자 프로그램을 동작시키기 위한 조합,
종종 /cgi-bin/으로 시작하는 주소를 볼 수 있는데, 웹 서버가 해당 디렉토리에 들어 있는 프로그램을 모두 다 CGI 프로그램으로 인식하도록 되어 있는 경우이다. - 우리 과제 환경
보안 문제와 가장 큰 문제는 요청이 하나 들어올때마다 프로세스가 하나씩 실행되기 때문에 최근에는 거의 쓰이지 않는다. */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  // 클라이언트에게 성공을 알려주는 응답 라인을 보내는 것으로 시작
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
  Rio_writen(fd, buf, strlen(buf));  // fd에 버퍼 넣는다
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (!strcasecmp(method, "HEAD")) // 같으면 0(false) 들어가고 끝냄(HEAD가 맞으면)
    return; // void 타입이라 바로 리턴해도 됨(끝내라)

  // 응답의 첫 번째 부분을 보낸 후, 새로운 자식 프로세스를 fork한다
  /* fork()를 사용하지 않으면 일반적으로 한 서버는 한 유저의 접속만 허용한다. 하지만 한 서버에 다수의 유저에 대한 서비스를 제공해주기 위해
  서버와 같은 일을 하는 프로ㅔ쓰를 생성시켜 새로 요청한 접속에 할당해주고, 부모는 새로운 요청이 들어오면 다시 자식 프로세스를 할당해 주는 방식으로 작동
  fork() 함수 호출이 성공하면 PID 값만 다른 똑같은 프로세스가 생성된다. 실행이 실패하면 -1을 반환하고 성공하면 부모 프로세스에게는 자식프로세스의 PID를,
  자식 프로세스에는 0을 반환한다. 원본 프로세스가 부모프로세스이고 복사된 프로세스가 자식 프로세스이다. */
  if (Fork() == 0) { /* Child */ // 0 : 정삭적으로 fork됨. 함수보면 <0이면 에러임
    /* Real server would set all CGI vars here */

    // cgiargs에 arguments 2개가 들어있음 (parse에서 물음표 기준으로 2개로 나눴음 strcpy)
    /* 예시
    uri : /cgi-bin/adder?123&123
    ->
    cgiargs : 123&123
    filename : ./cgi-bin/adder */

    // fork한 곳에서 set env 바꿔줌
    setenv("QUERY_STRING", cgiargs, 1); // 환경변수 설정. QUERY_STRING이라는 환경변수에 요청 URI의 CGI인자들(cgiargs)로 설정해준다. 1 : 우선순위 순서
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */ // 자식의 표준 출력을 연결 파일 식별자로 재지정 // fd를 STDOUT자리로 복사(자식에게 복사)
    Execve(filename, emptylist, environ); /* Run CGI program */ // 그 후 CGI프로그램 로드 후 실행
  }
  Wait(NULL); /* Parent waits for and reaps child */ // 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait 함수에서 블록된다(대기하는 함수)
}
