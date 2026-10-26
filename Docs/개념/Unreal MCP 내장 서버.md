# Unreal MCP 내장 서버

> UE 5.8의 `ModelContextProtocol` 플러그인("Unreal MCP", Experimental). 이 플러그인의 `age_*` 도구가 올라타는 서버. 엔진 소스로 확인한 사실만 적는다.

## 구성
- 모듈: `ModelContextProtocol`(서버 본체), `ModelContextProtocolEngine`(설정, 포트·경로), `ModelContextProtocolEditor`(ToolsetRegistry 연결).
- 기본 주소 `http://127.0.0.1:8000/mcp`. 설정 `UModelContextProtocolSettings`(포트, 경로, 자동 시작, 도구 검색 모드). 실행 인자 `-ModelContextProtocolPort=N`.
- 엔진 툴셋(`Experimental/Toolsets/*`, 대부분 Python 정의)은 "도구 검색 모드"에서 최상위에 `list_toolsets` → `describe_toolset` → `call_tool` 세 개만 보이고, 실제 도구는 `call_tool`로 부른다. 문자열 결과는 `{"returnValue": "..."}`로 한 번 더 감싸진다.
- 2026-10-23 조사: 툴셋 52개, 도구 833개, 전부 에디터가 있어야 함. AnimBP·GA/GE·몽타주 전용 도구 없음. 나이아가라 이미터 구조에 Simulation Stage 없음.

## 우리가 쓰는 방법
- **도구 직접 등록**: `IModelContextProtocolModule::AddTool(TSharedRef<IModelContextProtocolTool>)`. `tools/list` 최상위에 바로 나오고 결과 형식을 우리가 정한다. `OnRefreshTools`(새로고침 때 모든 도구가 지워짐)에 맞춰 다시 등록.
- 도구 인터페이스: `GetName`, `GetDescription`, `GetInputJsonSchema`, `Run`(게임 스레드). 결과는 `UE::ModelContextProtocol::MakeTextResult` / `MakeErrorResult`(`isError`).
- **커맨드렛에서 서버 켜기**: 자동 시작 설정은 커맨드렛을 건너뛴다(`ShouldAutoStartServer`). 그래서 상주 커맨드렛이 `StartServer(포트, 경로)`를 직접 부르고, 루프에서 `FTSTicker`를 돌린다(HTTP 서버와 MCP 서버가 ticker로 움직임) → [[커맨드렛과 상주 서버]].
- **HTTP 라우터 공유**: 서버는 `FHttpServerModule::GetHttpRouter(포트)`에 경로를 붙인다. 같은 라우터에 다른 경로(`/age/activity`)를 붙여 활동 창 페이지를 낸다. 리스너 설정의 기본 바인드 주소는 엔진 ini `[HTTPServer.Listeners]`를 따른다(엔진 기본 ini에는 값이 없음).

## 호출이 남기는 흔적 (활동 창의 근거)
- 서버는 모든 도구 호출마다 `LogModelContextProtocol: Running tool: '<이름>'`(Log 수준)을 남긴다. 세션 시작 때 `Session initialized: <id>`.
- 인자와 결과 전문은 VeryVerbose 로그에만 있어서 기본 설정으로는 안 보인다. 그래서 내장 도구는 이름만 알 수 있고, `call_tool` 안에서 실제로 부른 도구 이름도 보이지 않는다.
- 분석(analytics) 이벤트는 도구 이름을 해시로 바꿔 보내므로 쓸 수 없다.

## 주의
- Experimental이라 엔진 버전마다 API가 바뀔 수 있다. 그래서 MCP 부분은 별도 플러그인(`AssetGraphExporterMcp`)으로 분리.
- 에디터 모드에서는 호출마다 약 0.33초(빈 `tools/list`도 같음) — 에디터 프레임 간격으로 추정, 미검증.
