const state = document.getElementById('offline-state');
if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('./sw.js', { updateViaCache: 'none' }).then(async registration => {
    function show() {
      if (registration.active && !registration.installing) state.textContent = '资源已缓存，可断网使用';
    }
    show();
    registration.addEventListener('updatefound', () => {
      state.textContent = '正在准备离线资源…';
      const installing = registration.installing;
      installing?.addEventListener('statechange', () => {
        show();
        if (installing.state === 'redundant') state.textContent = '离线资源更新失败，请联网刷新重试';
      });
    });
    navigator.serviceWorker.addEventListener('controllerchange', show);
    await navigator.serviceWorker.ready;
    show();
  }).catch(() => { state.textContent = '离线资源未就绪，当前仍可本地识别'; });
} else state.textContent = '图片在本地处理；离线缓存需要 HTTPS';
