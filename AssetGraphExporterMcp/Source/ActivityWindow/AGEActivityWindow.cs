// Asset Graph Exporter
//
// A small window that shows what the MCP client (e.g. Claude) is doing, read from the activity page's JSON
// (<url>?after=N). It is its own process, so it stays open when the server stops and carries on when a server comes back.
// One window per address; a second start only brings the first to the front (or does nothing with --auto).
//
// Built by Scripts\Build.bat with the C# compiler that ships with Windows (.NET Framework 4, so C# 5 only).
// Usage: AGEActivityWindow.exe [url] [--auto]    --auto: started by the plugin, not by the user.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace AGE
{
	/** Windows language: Korean for Korean, English for every other (the page follows the browser the same way). */
	static class Text
	{
		public static readonly bool Korean = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ko";

		public static string Title { get { return Korean ? "Claude 활동" : "Claude Activity"; } }
		public static string Working { get { return Korean ? "작업 중" : "Working"; } }
		public static string Idle { get { return Korean ? "대기 중" : "Waiting"; } }
		public static string WaitingForServer { get { return Korean ? "서버를 기다리는 중" : "Waiting for the server"; } }
		public static string ServerAnswering { get { return Korean ? "서버 연결됨" : "Connected to the server"; } }
		public static string ServerOff { get { return Korean ? "서버 꺼짐" : "Server is off"; } }
		public static string ServerBusy { get { return Korean ? "서버 응답 없음 (바쁨)" : "No answer from the server (busy)"; } }
		public static string ServerStarted { get { return Korean ? "서버가 켜졌습니다" : "The server is on"; } }
		public static string ServerStopped { get { return Korean ? "서버가 꺼졌습니다" : "The server stopped"; } }
		public static string Connected { get { return Korean ? "AI가 연결했습니다" : "An AI connected"; } }
		public static string Finished { get { return Korean ? "끝났습니다" : "Finished"; } }
		public static string FinishedDetail { get { return Korean ? "1분 동안 새 요청이 없었습니다" : "No new request for a minute"; } }
		public static string Serve { get { return Korean ? "상주 서버 (저장된 파일)" : "Background server (saved files)"; } }
		public static string Editor { get { return Korean ? "에디터" : "Editor"; } }
		public static string NoActivity { get { return Korean ? "아직 활동 없음" : "No activity yet"; } }
		public static string JustNow { get { return Korean ? "방금" : "just now"; } }
		public static string OptionsMenu { get { return Korean ? "옵션" : "Options"; } }
		public static string WhenClosed { get { return Korean ? "창을 닫았을 때:" : "When this window is closed:"; } }
		public static string ReopenNextWork { get { return Korean ? "다음 AI 작업 때 다시 열기" : "Open again with the next AI work"; } }
		public static string StayClosedToday { get { return Korean ? "오늘은 다시 열지 않기" : "Don't open again today"; } }
		public static string ThemeCaption { get { return Korean ? "테마:" : "Theme:"; } }
		public static string ThemeSystem { get { return Korean ? "Windows 설정 따르기" : "Follow Windows"; } }
		public static string ThemeLight { get { return Korean ? "밝게" : "Light"; } }
		public static string ThemeDark { get { return Korean ? "어둡게" : "Dark"; } }
		public static string[] Columns
		{
			get
			{
				return Korean
					? new[] { "시각", "작업", "대상", "읽은 곳", "크기", "걸린 시간", "내용" }
					: new[] { "Time", "Action", "Target", "Read from", "Size", "Took", "Details" };
			}
		}

		public static string Ago(double seconds)
		{
			int minutes = (int)(seconds / 60);
			if (minutes < 1) return JustNow;
			if (minutes < 60) return minutes + (Korean ? "분 전" : " min ago");
			return (minutes / 60) + (Korean ? "시간 전" : " h ago");
		}

		public static string Last(string what, string ago) { return (Korean ? "마지막: " : "Last: ") + what + " · " + ago; }
		public static string Counts(int age, int engine)
		{
			return Korean ? "이 플러그인 도구 " + age + "번 · 엔진 내장 도구 " + engine + "번" : "This plugin's tools: " + age + " · Built-in engine tools: " + engine;
		}
		public static string Size(int chars)
		{
			if (chars >= 1000) return (chars / 1000.0).ToString("0.0", CultureInfo.InvariantCulture) + (Korean ? "K자" : "K chars");
			return chars + (Korean ? "자" : " chars");
		}
		public static string Source(string source)
		{
			if (source == "file") return Korean ? "저장된 파일" : "Saved file";
			if (source == "editor") return Korean ? "에디터" : "Editor";
			if (source == "editorUnsaved") return Korean ? "에디터 (저장 안 한 변경 포함)" : "Editor (with unsaved changes)";
			return source;
		}

		static readonly Dictionary<string, string[]> Names = new Dictionary<string, string[]>
		{
			{ "age_find_assets", new[] { "에셋 찾기", "Find assets" } },
			{ "age_references", new[] { "참조 관계", "References" } },
			{ "age_find_usages", new[] { "사용처 찾기", "Find usages" } },
			{ "age_diff", new[] { "버전 비교", "Revision diff" } },
			{ "age_guide", new[] { "사용 안내 읽기", "Read the guide" } },
			{ "age_feedback", new[] { "피드백 남기기", "Leave feedback" } },
			{ "age_get_asset", new[] { "에셋 구조 읽기", "Read asset structure" } },
			{ "age_blueprint_overview", new[] { "블루프린트 요약", "Blueprint summary" } },
			{ "age_statetree_overview", new[] { "StateTree 요약", "StateTree summary" } },
			{ "age_statetree_bindings", new[] { "StateTree 바인딩", "StateTree bindings" } },
			{ "age_animbp_overview", new[] { "애님 블루프린트 요약", "Anim Blueprint summary" } },
			{ "age_niagara_overview", new[] { "나이아가라 요약", "Niagara summary" } },
			{ "age_level_actors", new[] { "레벨 액터 읽기", "Read level actors" } },
			{ "age_status", new[] { "상태 확인", "Check status" } },
			{ "age_shutdown", new[] { "서버 종료 요청", "Request server shutdown" } },
			{ "age_open_window", new[] { "활동 창 열기", "Open the activity window" } },
			{ "list_toolsets", new[] { "엔진 도구 목록 보기", "List engine tools" } },
			{ "describe_toolset", new[] { "엔진 도구 설명 보기", "Describe engine tools" } },
			{ "call_tool", new[] { "엔진 도구 실행", "Run engine tool" } },
		};

		public static string ToolName(string tool)
		{
			string[] names;
			return Names.TryGetValue(tool, out names) ? names[Korean ? 0 : 1] : tool;
		}
	}

	/** One small file per choice, in the user's folder. */
	static class Settings
	{
		const string HiddenDateFile = "ActivityWindowHiddenDate.txt";
		const string ThemeFile = "ActivityWindowTheme.txt";
		public const string ThemeSystem = "system";
		public const string ThemeLight = "light";
		public const string ThemeDark = "dark";

		static string Today { get { return DateTime.Now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture); } }

		/** A date, so the choice ends by itself the next day. */
		public static bool StayClosedToday
		{
			get { return Read(HiddenDateFile) == Today; }
			set { Write(HiddenDateFile, value ? Today : ""); }
		}

		public static string Theme
		{
			get
			{
				string theme = Read(ThemeFile);
				return theme == ThemeLight || theme == ThemeDark ? theme : ThemeSystem;
			}
			set { Write(ThemeFile, value); }
		}

		static string FilePath(string name)
		{
			return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AssetGraphExporter", name);
		}

		static string Read(string name)
		{
			try { return File.ReadAllText(FilePath(name)).Trim(); }
			catch (IOException) { return ""; }
			catch (UnauthorizedAccessException) { return ""; }
		}

		static void Write(string name, string value)
		{
			try
			{
				Directory.CreateDirectory(Path.GetDirectoryName(FilePath(name)));
				File.WriteAllText(FilePath(name), value);
			}
			catch (IOException) { }
			catch (UnauthorizedAccessException) { }
		}
	}

	enum ServerState { Unknown, Answering, Busy, Off }
	enum RowKind { Tool, Engine, Notice, Finished, Error }

	/** The same colors as the activity page, in its light and dark forms. */
	class Palette
	{
		public bool bDark;
		public Color Back, Panel, Line, Foreground, Muted, Tool, Notice, Ok, Error;

		public static readonly Palette Light = new Palette
		{
			Back = Color.White, Panel = Color.FromArgb(246, 245, 242), Line = Color.FromArgb(228, 224, 216),
			Foreground = Color.FromArgb(34, 32, 28), Muted = Color.FromArgb(111, 106, 98),
			Tool = Color.FromArgb(47, 111, 176), Notice = Color.FromArgb(194, 89, 47), Ok = Color.FromArgb(46, 125, 79), Error = Color.FromArgb(179, 38, 30),
		};
		public static readonly Palette Dark = new Palette
		{
			bDark = true,
			Back = Color.FromArgb(27, 26, 24), Panel = Color.FromArgb(36, 34, 31), Line = Color.FromArgb(54, 51, 46),
			Foreground = Color.FromArgb(236, 232, 225), Muted = Color.FromArgb(163, 157, 147),
			Tool = Color.FromArgb(127, 178, 229), Notice = Color.FromArgb(224, 131, 90), Ok = Color.FromArgb(108, 194, 143), Error = Color.FromArgb(240, 138, 128),
		};

		public Color ForRow(RowKind kind)
		{
			switch (kind)
			{
				case RowKind.Tool: return Tool;
				case RowKind.Engine: return Muted;
				case RowKind.Notice: return Notice;
				case RowKind.Finished: return Ok;
				default: return Error;
			}
		}

		public static Palette Current()
		{
			string theme = Settings.Theme;
			if (theme == Settings.ThemeLight) return Light;
			if (theme == Settings.ThemeDark) return Dark;
			object appsUseLight = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1);
			return appsUseLight is int && (int)appsUseLight == 0 ? Dark : Light;
		}
	}

	/** Menu and status bar colors. */
	class PaletteColors : ProfessionalColorTable
	{
		readonly Palette P;
		public PaletteColors(Palette palette) { P = palette; UseSystemColors = false; }

		public override Color MenuStripGradientBegin { get { return P.Panel; } }
		public override Color MenuStripGradientEnd { get { return P.Panel; } }
		public override Color StatusStripGradientBegin { get { return P.Panel; } }
		public override Color StatusStripGradientEnd { get { return P.Panel; } }
		public override Color ToolStripDropDownBackground { get { return P.Panel; } }
		public override Color ImageMarginGradientBegin { get { return P.Panel; } }
		public override Color ImageMarginGradientMiddle { get { return P.Panel; } }
		public override Color ImageMarginGradientEnd { get { return P.Panel; } }
		public override Color MenuBorder { get { return P.Line; } }
		public override Color MenuItemBorder { get { return P.Line; } }
		public override Color MenuItemSelected { get { return P.Line; } }
		public override Color MenuItemSelectedGradientBegin { get { return P.Line; } }
		public override Color MenuItemSelectedGradientEnd { get { return P.Line; } }
		public override Color MenuItemPressedGradientBegin { get { return P.Line; } }
		public override Color MenuItemPressedGradientEnd { get { return P.Line; } }
		public override Color CheckBackground { get { return P.Line; } }
		public override Color CheckSelectedBackground { get { return P.Line; } }
		public override Color CheckPressedBackground { get { return P.Line; } }
		public override Color SeparatorDark { get { return P.Line; } }
		public override Color SeparatorLight { get { return P.Panel; } }
	}

	/** The status bar's top line is a fixed light color, which would stand out on the dark theme. */
	class PaletteRenderer : ToolStripProfessionalRenderer
	{
		public PaletteRenderer(Palette palette) : base(new PaletteColors(palette)) { }

		protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e)
		{
			if (!(e.ToolStrip is StatusStrip)) base.OnRenderToolStripBorder(e);
		}
	}

	/** A list that does not flicker when rows are added once a second. */
	class ActivityList : ListView
	{
		public ActivityList() { DoubleBuffered = true; }
	}

	class ActivityForm : Form
	{
		const int MaxRows = 5000;
		const int FinishedAfterSeconds = 60;
		const int RequestTimeoutMilliseconds = 10000;
		const int DetailsColumn = 6;
		const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;

		[DllImport("dwmapi.dll")]
		static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
		[DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
		static extern int SetWindowTheme(IntPtr window, string appName, string idList);

		readonly string Url;
		readonly bool bAuto;
		readonly EventWaitHandle ShowSignal;
		readonly ActivityList List;
		readonly Label StateLabel;
		readonly Label LastLabel;
		readonly ToolStripStatusLabel ServerLabel;
		readonly ToolStripStatusLabel ProjectLabel;
		readonly ToolStripStatusLabel CountsLabel;
		readonly Panel Header;
		readonly MenuStrip TopMenu;
		readonly StatusStrip Status;

		Palette Colors = Palette.Current();
		ServerState State = ServerState.Unknown;
		long LastId = 0;
		int AgeCalls = 0;
		int EngineCalls = 0;
		bool bWorkOpen = false;
		string LastText = null;
		/** From the server's idleSeconds, so it is right even for entries from before this window opened. */
		DateTime? LastActivity = null;

		public ActivityForm(string url, bool auto, EventWaitHandle showSignal)
		{
			Url = url;
			bAuto = auto;
			ShowSignal = showSignal;

			Text = AGE.Text.Title;
			// The window does not take the program's icon by itself.
			Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
			Size = new Size(1040, 620);
			MinimumSize = new Size(600, 320);
			Font = SystemFonts.MessageBoxFont;

			List = new ActivityList
			{
				Dock = DockStyle.Fill,
				View = View.Details,
				FullRowSelect = true,
				HeaderStyle = ColumnHeaderStyle.Nonclickable,
				ShowItemToolTips = true,
				BorderStyle = BorderStyle.None,
				// The column headers have no dark form of their own, so they are drawn here; rows stay the list's own.
				OwnerDraw = true,
			};
			List.DrawColumnHeader += DrawColumnHeader;
			List.DrawItem += (sender, args) => args.DrawDefault = true;
			List.DrawSubItem += (sender, args) => args.DrawDefault = true;
			int[] widths = { 72, 180, 170, 110, 70, 76, 300 };
			for (int i = 0; i < widths.Length; i++)
			{
				List.Columns.Add(AGE.Text.Columns[i], widths[i]);
			}
			List.Resize += (sender, args) => FitDetailsColumn();

			Header = new Panel { Dock = DockStyle.Top, Height = 58, Padding = new Padding(12, 8, 12, 6) };
			StateLabel = new Label { Dock = DockStyle.Top, Height = 25, Font = new Font(Font.FontFamily, 12f, FontStyle.Bold) };
			LastLabel = new Label { Dock = DockStyle.Top, Height = 20, AutoEllipsis = true };
			Header.Controls.Add(LastLabel);
			Header.Controls.Add(StateLabel);

			Status = new StatusStrip { SizingGrip = true };
			ServerLabel = new ToolStripStatusLabel();
			ProjectLabel = new ToolStripStatusLabel { Margin = new Padding(14, 3, 0, 2) };
			CountsLabel = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleRight };
			Status.Items.AddRange(new ToolStripItem[] { ServerLabel, ProjectLabel, CountsLabel });

			TopMenu = BuildMenu();
			Controls.Add(List);
			Controls.Add(Header);
			Controls.Add(TopMenu);
			Controls.Add(Status);
			ApplyTheme();

			// Follows Windows while the window is open.
			SystemEvents.UserPreferenceChanged += (sender, args) => { if (IsHandleCreated) BeginInvoke((MethodInvoker)ApplyTheme); };
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);
			ApplyTitleBar();
		}

		void ApplyTitleBar()
		{
			int dark = Colors.bDark ? 1 : 0;
			DwmSetWindowAttribute(Handle, DWMWA_USE_IMMERSIVE_DARK_MODE, ref dark, sizeof(int));
		}

		void ApplyTheme()
		{
			Colors = Palette.Current();
			BackColor = Colors.Back;
			List.BackColor = Colors.Back;
			List.ForeColor = Colors.Foreground;
			Header.BackColor = Colors.Panel;
			StateLabel.BackColor = Colors.Panel;
			LastLabel.BackColor = Colors.Panel;
			LastLabel.ForeColor = Colors.Muted;

			PaletteRenderer renderer = new PaletteRenderer(Colors);
			TopMenu.Renderer = renderer;
			Status.Renderer = renderer;
			TopMenu.ForeColor = Colors.Foreground;
			Status.ForeColor = Colors.Foreground;
			foreach (ToolStripItem top in TopMenu.Items)
			{
				top.ForeColor = Colors.Foreground;
				foreach (ToolStripItem item in ((ToolStripMenuItem)top).DropDownItems)
				{
					item.ForeColor = Colors.Foreground;
				}
			}
			foreach (ToolStripItem item in Status.Items)
			{
				item.ForeColor = Colors.Foreground;
			}
			foreach (ListViewItem row in List.Items)
			{
				row.ForeColor = Colors.ForRow((RowKind)row.Tag);
			}

			if (IsHandleCreated)
			{
				ApplyTitleBar();
				SetWindowTheme(List.Handle, Colors.bDark ? "DarkMode_Explorer" : "Explorer", null);
				List.Invalidate();
			}
			ShowState();
		}

		void DrawColumnHeader(object sender, DrawListViewColumnHeaderEventArgs e)
		{
			using (SolidBrush back = new SolidBrush(Colors.Panel))
			using (Pen line = new Pen(Colors.Line))
			{
				e.Graphics.FillRectangle(back, e.Bounds);
				e.Graphics.DrawLine(line, e.Bounds.Right - 1, e.Bounds.Top, e.Bounds.Right - 1, e.Bounds.Bottom);
				e.Graphics.DrawLine(line, e.Bounds.Left, e.Bounds.Bottom - 1, e.Bounds.Right, e.Bounds.Bottom - 1);
			}
			Rectangle text = new Rectangle(e.Bounds.Left + 6, e.Bounds.Top, e.Bounds.Width - 8, e.Bounds.Height);
			TextRenderer.DrawText(e.Graphics, e.Header.Text, List.Font, text, Colors.Muted, TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
		}

		/** Started by the plugin while the user works in Unreal: appear, but leave the keyboard where it is. */
		protected override bool ShowWithoutActivation { get { return bAuto; } }

		/** Not in the constructor: these threads call back into the window, which needs to exist by then. */
		protected override void OnShown(EventArgs e)
		{
			base.OnShown(e);
			SetWindowTheme(List.Handle, Colors.bDark ? "DarkMode_Explorer" : "Explorer", null);
			FitDetailsColumn();
			new Thread(PollLoop) { IsBackground = true }.Start();
			new Thread(ShowLoop) { IsBackground = true }.Start();
		}

		MenuStrip BuildMenu()
		{
			ToolStripMenuItem whenClosed = new ToolStripMenuItem(AGE.Text.WhenClosed) { Enabled = false };
			ToolStripMenuItem reopen = new ToolStripMenuItem(AGE.Text.ReopenNextWork);
			ToolStripMenuItem stayClosed = new ToolStripMenuItem(AGE.Text.StayClosedToday);
			ToolStripMenuItem themeCaption = new ToolStripMenuItem(AGE.Text.ThemeCaption) { Enabled = false };
			ToolStripMenuItem themeSystem = new ToolStripMenuItem(AGE.Text.ThemeSystem);
			ToolStripMenuItem themeLight = new ToolStripMenuItem(AGE.Text.ThemeLight);
			ToolStripMenuItem themeDark = new ToolStripMenuItem(AGE.Text.ThemeDark);
			ToolStripMenuItem options = new ToolStripMenuItem(AGE.Text.OptionsMenu);
			options.DropDownItems.AddRange(new ToolStripItem[] { whenClosed, reopen, stayClosed, new ToolStripSeparator(), themeCaption, themeSystem, themeLight, themeDark });
			options.DropDownOpening += (sender, args) =>
			{
				stayClosed.Checked = Settings.StayClosedToday;
				reopen.Checked = !stayClosed.Checked;
				string theme = Settings.Theme;
				themeSystem.Checked = theme == Settings.ThemeSystem;
				themeLight.Checked = theme == Settings.ThemeLight;
				themeDark.Checked = theme == Settings.ThemeDark;
			};
			reopen.Click += (sender, args) => Settings.StayClosedToday = false;
			stayClosed.Click += (sender, args) => Settings.StayClosedToday = true;
			themeSystem.Click += (sender, args) => { Settings.Theme = Settings.ThemeSystem; ApplyTheme(); };
			themeLight.Click += (sender, args) => { Settings.Theme = Settings.ThemeLight; ApplyTheme(); };
			themeDark.Click += (sender, args) => { Settings.Theme = Settings.ThemeDark; ApplyTheme(); };

			MenuStrip menu = new MenuStrip();
			menu.Items.Add(options);
			MainMenuStrip = menu;
			return menu;
		}

		/** The last column takes what the others leave. */
		void FitDetailsColumn()
		{
			int others = 0;
			for (int i = 0; i < DetailsColumn; i++)
			{
				others += List.Columns[i].Width;
			}
			List.Columns[DetailsColumn].Width = Math.Max(120, List.ClientSize.Width - others);
		}

		void ShowLoop()
		{
			while (true)
			{
				ShowSignal.WaitOne();
				BeginInvoke((MethodInvoker)delegate
				{
					if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
					Activate();
				});
			}
		}

		void PollLoop()
		{
			while (true)
			{
				string body = null;
				ServerState state = ServerState.Answering;
				try
				{
					HttpWebRequest request = (HttpWebRequest)WebRequest.Create(Url + "?after=" + Interlocked.Read(ref LastId));
					request.Proxy = null;
					request.Timeout = RequestTimeoutMilliseconds;
					request.ReadWriteTimeout = RequestTimeoutMilliseconds;
					using (WebResponse response = request.GetResponse())
					using (StreamReader reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8))
					{
						body = reader.ReadToEnd();
					}
				}
				catch (WebException e)
				{
					// Refused: nothing listens on the port. A timeout: the server is there but its game thread is busy.
					state = e.Status == WebExceptionStatus.Timeout ? ServerState.Busy : ServerState.Off;
				}
				catch (IOException)
				{
					state = ServerState.Off;
				}

				try
				{
					Invoke((MethodInvoker)delegate { Apply(state, body); });
				}
				catch (ObjectDisposedException) { return; }
				catch (InvalidOperationException) { return; }
				Thread.Sleep(1000);
			}
		}

		void Apply(ServerState state, string body)
		{
			Dictionary<string, object> data = null;
			if (body != null)
			{
				try { data = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(body); }
				catch (ArgumentException) { state = ServerState.Off; }
				catch (InvalidOperationException) { state = ServerState.Off; }
				// The route is not there yet (the server is starting), or another program answers on this port.
				if (data != null && !data.ContainsKey("last")) { data = null; state = ServerState.Off; }
			}

			if (state != State)
			{
				if (state == ServerState.Off && State != ServerState.Unknown)
				{
					AddRow(Now(), AGE.Text.ServerStopped, RowKind.Notice);
					bWorkOpen = false;
				}
				else if (state == ServerState.Answering && State != ServerState.Busy)
				{
					AddRow(Now(), AGE.Text.ServerStarted, RowKind.Notice);
				}
				State = state;
			}

			if (data != null)
			{
				long last = Convert.ToInt64(data["last"]);
				if (last < Interlocked.Read(ref LastId))
				{
					// The server was restarted: its numbering starts again. Earlier rows stay, this window keeps the day's history.
					Interlocked.Exchange(ref LastId, 0);
				}
				foreach (object item in (System.Collections.IEnumerable)data["entries"])
				{
					AddEntry((Dictionary<string, object>)item);
				}
				if (data.ContainsKey("idleSeconds"))
				{
					double idle = Convert.ToDouble(data["idleSeconds"]);
					LastActivity = DateTime.Now.AddSeconds(-idle);
					if (bWorkOpen && idle >= FinishedAfterSeconds)
					{
						AddRow(Now(), AGE.Text.Finished, RowKind.Finished).SubItems[DetailsColumn].Text = AGE.Text.FinishedDetail;
						bWorkOpen = false;
					}
				}
				ProjectLabel.Text = data["project"] + " · " + ((string)data["mode"] == "serve" ? AGE.Text.Serve : AGE.Text.Editor);
				CountsLabel.Text = AGE.Text.Counts(AgeCalls, EngineCalls);
			}
			ShowState();
		}

		void AddEntry(Dictionary<string, object> entry)
		{
			Interlocked.Exchange(ref LastId, Math.Max(Interlocked.Read(ref LastId), Convert.ToInt64(entry["id"])));
			string kind = (string)entry["kind"];
			string what;
			RowKind rowKind;
			if (kind == "tool")
			{
				AgeCalls++;
				what = AGE.Text.ToolName((string)entry["tool"]);
				rowKind = RowKind.Tool;
			}
			else if (kind == "engineTool")
			{
				EngineCalls++;
				what = AGE.Text.ToolName((string)entry["tool"]);
				rowKind = RowKind.Engine;
			}
			else
			{
				what = AGE.Text.Connected;
				rowKind = RowKind.Notice;
			}

			ListViewItem row = AddRow((string)entry["time"], what, rowKind);
			string target = Target(entry);
			row.SubItems[2].Text = target;
			object value;
			if (entry.TryGetValue("source", out value)) row.SubItems[3].Text = AGE.Text.Source((string)value);
			if (entry.TryGetValue("chars", out value)) row.SubItems[4].Text = AGE.Text.Size(Convert.ToInt32(value));
			if (entry.TryGetValue("ms", out value)) row.SubItems[5].Text = Convert.ToInt64(value) + "ms";
			if (entry.TryGetValue("line", out value)) row.SubItems[DetailsColumn].Text = (string)value;
			if (entry.TryGetValue("error", out value))
			{
				row.SubItems[DetailsColumn].Text = (string)value;
				row.Tag = RowKind.Error;
				row.ForeColor = Colors.Error;
			}
			// The whole text of a long detail, on hover.
			row.ToolTipText = row.SubItems[DetailsColumn].Text;

			// A client that only connected has not started any work.
			bWorkOpen = bWorkOpen || rowKind != RowKind.Notice;
			LastText = target.Length > 0 ? what + " " + target : what;
		}

		static string Target(Dictionary<string, object> entry)
		{
			object args;
			if (!entry.TryGetValue("args", out args)) return "";
			Dictionary<string, object> values = (Dictionary<string, object>)args;
			List<string> parts = new List<string>();
			foreach (string key in new[] { "asset", "symbol", "select", "name", "class", "filter" })
			{
				object value;
				if (!values.TryGetValue(key, out value)) continue;
				string text = (string)value;
				if (key == "asset")
				{
					// /Game/Folder/BP_Thing.BP_Thing -> BP_Thing
					text = text.Substring(text.LastIndexOf('/') + 1);
					int dot = text.IndexOf('.');
					if (dot > 0) text = text.Substring(0, dot);
				}
				parts.Add(text);
			}
			return string.Join(" · ", parts);
		}

		static string Now() { return DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture); }

		void ShowState()
		{
			if (State == ServerState.Off)
			{
				StateLabel.Text = AGE.Text.ServerOff;
				StateLabel.ForeColor = Colors.Error;
				ServerLabel.Text = AGE.Text.ServerOff;
			}
			else if (State == ServerState.Busy)
			{
				StateLabel.Text = AGE.Text.ServerBusy;
				StateLabel.ForeColor = Colors.Notice;
				ServerLabel.Text = AGE.Text.ServerBusy;
			}
			else if (State == ServerState.Answering)
			{
				StateLabel.Text = bWorkOpen ? AGE.Text.Working : AGE.Text.Idle;
				StateLabel.ForeColor = bWorkOpen ? Colors.Tool : Colors.Ok;
				ServerLabel.Text = AGE.Text.ServerAnswering;
			}
			else
			{
				StateLabel.Text = AGE.Text.WaitingForServer;
				StateLabel.ForeColor = Colors.Muted;
			}
			LastLabel.Text = LastText != null && LastActivity.HasValue
				? AGE.Text.Last(LastText, AGE.Text.Ago((DateTime.Now - LastActivity.Value).TotalSeconds))
				: AGE.Text.NoActivity;
		}

		/** Follows new rows, unless the user has scrolled up to read. */
		ListViewItem AddRow(string time, string what, RowKind kind)
		{
			int count = List.Items.Count;
			bool bAtBottom = count == 0 || List.Items[count - 1].Bounds.Top < List.ClientSize.Height;

			ListViewItem row = new ListViewItem(new[] { time, what, "", "", "", "", "" }) { ForeColor = Colors.ForRow(kind), Tag = kind };
			List.Items.Add(row);
			if (List.Items.Count > MaxRows)
			{
				List.Items.RemoveAt(0);
			}
			if (bAtBottom)
			{
				row.EnsureVisible();
			}
			return row;
		}
	}

	static class Program
	{
		const string DefaultUrl = "http://127.0.0.1:8000/age/activity";

		[DllImport("user32.dll")]
		static extern bool SetProcessDPIAware();

		[STAThread]
		static void Main(string[] args)
		{
			string url = DefaultUrl;
			bool bAuto = false;
			foreach (string arg in args)
			{
				if (arg == "--auto") bAuto = true;
				else url = arg;
			}

			string id = "AGEActivityWindow_" + new Uri(url).Port;
			bool bFirst;
			using (Mutex single = new Mutex(true, id, out bFirst))
			using (EventWaitHandle showSignal = new EventWaitHandle(false, EventResetMode.AutoReset, id + "_Show"))
			{
				if (!bFirst)
				{
					if (!bAuto) showSignal.Set();
					return;
				}
				if (bAuto && Settings.StayClosedToday)
				{
					return;
				}
				SetProcessDPIAware();
				Application.EnableVisualStyles();
				Application.SetCompatibleTextRenderingDefault(false);
				Application.Run(new ActivityForm(url, bAuto, showSignal));
				GC.KeepAlive(single);
			}
		}
	}
}
