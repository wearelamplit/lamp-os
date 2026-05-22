<!-- eslint-disable vue/multi-word-component-names -->
<script setup lang="ts">
import LamplitLogo from '@/components/LamplitLogo.vue'
</script>

<template>
  <section class="tab-panel" aria-label="Information">
    <div class="info-content">
      <div class="logo-container">
        <LamplitLogo style="width: 50%; max-width: 200px;" />
      </div>

      <div class="tagline">
        <span class="tagline-icon">✦</span>
        Sparking inspiration through shared creative experiences
      </div>

      <div class="info-section">
        <h3 class="section-title">
          <span class="title-accent">About</span> Lamplit Art Society
        </h3>
        <p>
          A non-profit collective dedicated to sparking inspiration and
          providing opportunities for people to <em>connect</em>, <em>celebrate</em>,
          <em>grow</em>, and <em>inspire</em> others through shared creative experiences.
        </p>
      </div>

      <div class="info-section">
        <h3 class="section-title">
          <span class="title-accent">The</span> Lamps
        </h3>
        <p>
          The art project from which our society grew. Their surreal and vivid presence
          captivates audiences, fosters unexpected connections, inspires creativity and play,
          and illuminates spaces.
        </p>
      </div>

      <div class="info-section">
        <h3 class="section-title">
          <span class="title-accent">Open</span> Source
        </h3>
        <p>
          As stewards of this decentralized project, we maintain its core vision while
          welcoming contributors and artists to build, adopt, or share these lamps with their
          communities.
        </p>
      </div>

      <div class="footer-link">
        <span class="link-decoration"></span>
        <a href="https://lamplit.ca" target="_blank" rel="noopener">lamplit.ca</a>
        <span class="link-decoration"></span>
      </div>
    </div>
  </section>
</template>

<style scoped>
.tab-panel {
  animation: fadeIn 0.3s ease-in-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.info-content {
  padding: 16px 20px;
}

.logo-container {
  display: flex;
  justify-content: center;
  margin-bottom: 24px;
}

.tagline {
  text-align: center;
  font-size: 0.95rem;
  font-style: italic;
  color: var(--brand-glow-pink);
  margin-bottom: 32px;
  line-height: 1.5;
}

.tagline-icon {
  display: inline-block;
  margin-right: 8px;
  color: var(--brand-amber-gold);
  animation: sparkle 2s ease-in-out infinite;
}

@keyframes sparkle {
  0%, 100% {
    opacity: 1;
    transform: scale(1);
  }
  50% {
    opacity: 0.6;
    transform: scale(0.9);
  }
}

.info-section {
  margin-bottom: 24px;
}

.section-title {
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--brand-lamp-white);
  margin: 0 0 8px 0;
}

.title-accent {
  color: var(--brand-lumen-green);
  font-weight: 700;
}

.info-section p {
  margin: 0;
  font-size: 0.9rem;
  line-height: 1.7;
  color: var(--brand-fog-grey);
}

.info-section em {
  font-style: normal;
  color: var(--brand-amber-gold);
}

.footer-link {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 16px;
  margin-top: 32px;
  padding-top: 24px;
  border-top: 1px solid rgba(141, 205, 166, 0.2);
}

.link-decoration {
  width: 40px;
  height: 2px;
  background: linear-gradient(
    90deg,
    transparent,
    var(--brand-glow-pink),
    transparent
  );
  border-radius: 1px;
}

.footer-link a {
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--brand-aurora-blue);
  text-decoration: none;
  transition: all 0.2s ease;
}

.footer-link a:hover {
  color: var(--brand-glow-pink);
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .info-content {
    padding: 12px 16px;
  }

  .tagline {
    font-size: 0.9rem;
  }
}
</style>
