/**
 * Dynamite 重构项目 —— iOS 启动壳（Stub）
 *
 * 职责：
 * - 作为 iOS 应用的入口点，初始化 UIApplication
 * - 创建 UIWindow 和根视图控制器
 * - 加载 SDL3 iOS 主运行循环
 * - 初始化 Go DataLayer（gomobile 生成 framework）
 *
 * TODO:
 * - 集成 GoMobile.framework 后，在 application:didFinishLaunchingWithOptions:
 *   中调用 Go 数据层初始化函数
 * - 如需支持横屏/竖屏切换，补全 supportedInterfaceOrientationsForWindow
 */

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <string>

// -----------------------------------------------------------------------------
// 全局常量
// -----------------------------------------------------------------------------
/** 应用窗口初始尺寸（iPhone 标准竖屏逻辑分辨率） */
static const CGFloat kInitialWindowWidth  = 375.0f;
static const CGFloat kInitialWindowHeight = 812.0f;

// -----------------------------------------------------------------------------
// AppDelegate 接口声明
// -----------------------------------------------------------------------------
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation AppDelegate

/**
 * 应用启动完成回调
 *
 * 初始化流程：
 * 1. 创建 UIWindow
 * 2. 设置根视图控制器（由 SDL3 接管渲染）
 * 3. 初始化 Go DataLayer
 * 4. 使窗口可见
 *
 * @param application UIApplication 实例
 * @param launchOptions 启动参数
 * @return 启动成功返回 YES
 */
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)launchOptions;

    NSLog(@"[AppDelegate] Dynamite iOS 启动");

    // 1. 创建主窗口
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

    // 2. 设置根视图控制器
    // TODO: SDL3 iOS 模板会创建自己的 ViewController，此处预留接口
    UIViewController *rootViewController = [[UIViewController alloc] init];
    rootViewController.view.backgroundColor = [UIColor blackColor];
    self.window.rootViewController = rootViewController;

    // 3. 初始化 Go DataLayer
    // TODO: 链接 GoMobile.framework 后，调用其初始化函数
    // 示例：GoDataInit([[NSBundle mainBundle] bundlePath]);
    [self initGoDataLayer];

    // 4. 显示窗口
    [self.window makeKeyAndVisible];

    return YES;
}

/**
 * 初始化 Go 数据层
 *
 * TODO: gomobile 生成 framework 后，通过 extern "C" 或 ObjC 封装调用
 */
- (void)initGoDataLayer {
    NSLog(@"[AppDelegate] Go DataLayer 初始化（当前为 stub）");
    // 获取应用沙盒路径，传给 C++/Go 层
    NSString *docsPath = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    NSLog(@"[AppDelegate] Document 目录: %@", docsPath);
}

/**
 * 应用进入后台
 *
 * 触发场景：用户按下 Home 键、切换应用、接听电话
 * TODO: 暂停音频播放、暂停游戏逻辑更新
 */
- (void)applicationDidEnterBackground:(UIApplication *)application {
    (void)application;
    NSLog(@"[AppDelegate] 应用进入后台");
}

/**
 * 应用返回前台
 *
 * TODO: 恢复音频播放、恢复游戏逻辑更新
 */
- (void)applicationWillEnterForeground:(UIApplication *)application {
    (void)application;
    NSLog(@"[AppDelegate] 应用返回前台");
}

/**
 * 应用收到内存警告
 *
 * 响应策略：释放非必要缓存（如已解析但未使用的谱面数据、纹理缓存中的低频资源）
 */
- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application {
    (void)application;
    NSLog(@"[AppDelegate] 收到内存警告，建议释放缓存");
}

/**
 * 应用终止前回调
 *
 * TODO: 保存用户配置、刷新待同步成绩到本地数据库
 */
- (void)applicationWillTerminate:(UIApplication *)application {
    (void)application;
    NSLog(@"[AppDelegate] 应用即将终止");
}

@end

/**
 * iOS 应用标准入口
 *
 * UIApplicationMain 负责：
 * - 创建 UIApplication 单例
 * - 创建 AppDelegate 实例
 * - 启动主运行循环（RunLoop）
 */
int main(int argc, char * argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
